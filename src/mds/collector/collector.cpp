#include "collector.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "../inode/InodeStorage.h"
#include "../../debug/ZBLog.h"
#include "../../srm/image_manager/ImageManager.h"
#ifdef INODE_DISK_SLOT_SIZE
#undef INODE_DISK_SLOT_SIZE
#endif

namespace {

namespace fs = std::filesystem;

constexpr int kTimestampYearBase = 2000;

std::chrono::system_clock::time_point to_time_point(const InodeTimestamp& ts) {
	std::tm tm{};
	int year = kTimestampYearBase + static_cast<int>(ts.year);
	if (year < 1970) {
		year = 1970;
	}
	tm.tm_year = year - 1900;
	tm.tm_mon = std::max(0, static_cast<int>(ts.month) - 1);
	tm.tm_mday = std::max(1, static_cast<int>(ts.day));
	tm.tm_hour = static_cast<int>(ts.hour);
	tm.tm_min = static_cast<int>(ts.minute);
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	std::time_t tt = std::mktime(&tm);
	if (tt == -1) {
		return std::chrono::system_clock::time_point{};
	}
	return std::chrono::system_clock::from_time_t(tt);
}

bool inode_in_range(uint64_t ino, const ColdScanRange& range) {
	if (range.start_ino != 0 && ino < range.start_ino) {
		return false;
	}
	if (range.end_ino != 0 && ino > range.end_ino) {
		return false;
	}
	return true;
}

bool is_cold_default(const Inode& inode, const ColdCollectorConfig& cfg) {
	const auto now = std::chrono::system_clock::now();
	const auto access_tp = to_time_point(inode.fa_time);
	if (access_tp.time_since_epoch().count() == 0) {
		return false;
	}
	return now - access_tp >= cfg.cold_threshold;
}

} // namespace

ColdDataCollectorService::ColdDataCollectorService(MdsServer* mds,
									  srm::ImageManager* image_mgr,
									  ColdCollectorConfig cfg)
	: mds_(mds), image_mgr_(image_mgr), config_(std::move(cfg)) {}

ColdDataCollectorService::~ColdDataCollectorService() { stop(); }

void ColdDataCollectorService::start() {
	bool expected = false;
	if (!running_.compare_exchange_strong(expected, true)) {
		return;
	}
	worker_ = std::thread(&ColdDataCollectorService::run_loop, this);
}

void ColdDataCollectorService::stop() {
	if (!running_.exchange(false)) {
		return;
	}
	if (worker_.joinable()) {
		worker_.join();
	}
}

void ColdDataCollectorService::update_config(const ColdCollectorConfig& cfg) {
	std::lock_guard<std::mutex> lock(config_mtx_);
	config_ = cfg;
}

void ColdDataCollectorService::set_selector(std::shared_ptr<IColdInodeSelector> selector) {
	std::lock_guard<std::mutex> lock(hook_mtx_);
	selector_ = std::move(selector);
}

void ColdDataCollectorService::set_scheduler(std::shared_ptr<IImageAggregationScheduler> scheduler) {
	std::lock_guard<std::mutex> lock(hook_mtx_);
	scheduler_ = std::move(scheduler);
}

ColdScanResult ColdDataCollectorService::run_single_scan_for_test() {
	return scan_once();
}

void ColdDataCollectorService::run_loop() {
	while (running_.load()) {
		auto loop_start = std::chrono::steady_clock::now();
		ColdScanResult result = scan_once();
		if (!result.cold_inodes.empty()) {
			buffer_pending_inodes(result);
			flush_pending_if_needed(false);
		}
		ColdCollectorConfig cfg = snapshot_config();
		auto interval = std::chrono::duration_cast<std::chrono::seconds>(cfg.scan_interval);
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - loop_start);
		if (!running_.load()) {
			break;
		}
		if (interval.count() <= 0) {
			continue;
		}
		if (elapsed < interval) {
			std::this_thread::sleep_for(interval - elapsed);
		}
	}
	flush_pending_if_needed(true);
}

ColdScanResult ColdDataCollectorService::scan_once() {
	ColdScanResult result;
	result.collected_at = std::chrono::system_clock::now();
	ColdCollectorConfig cfg = snapshot_config();
	std::shared_ptr<IColdInodeSelector> selector;
	{
		std::lock_guard<std::mutex> lock(hook_mtx_);
		selector = selector_;
	}

	std::error_code ec;
	if (!fs::exists(cfg.inode_directory, ec)) {
		LOGW("collector: inode directory missing -> " << cfg.inode_directory);
		return result;
	}

	std::vector<fs::path> batch_files;
	for (const auto& entry : fs::directory_iterator(cfg.inode_directory, ec)) {
		if (ec) {
			break;
		}
		if (entry.is_regular_file(ec)) {
			batch_files.push_back(entry.path());
		}
	}
	std::sort(batch_files.begin(), batch_files.end());

	std::vector<uint8_t> buffer(InodeStorage::INODE_DISK_SLOT_SIZE, 0);
	size_t inspected = 0;
	for (const auto& path : batch_files) {
		if (inspected >= cfg.max_inodes_per_round) {
			break;
		}
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			LOGW("collector: failed to open batch file " << path);
			continue;
		}
		while (inspected < cfg.max_inodes_per_round && in.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) {
			size_t offset = 0;
			Inode inode;
			if (!Inode::deserialize(buffer.data(), offset, inode, buffer.size())) {
				continue;
			}
			if (!inode_in_range(inode.inode, cfg.scan_range)) {
				continue;
			}
			++inspected;
			bool cold = selector ? selector->is_cold(inode, cfg) : is_cold_default(inode, cfg);
			if (cold) {
				result.cold_inodes.push_back(inode.inode);
				result.inode_records.push_back(inode);
				if (result.cold_inodes.size() >= cfg.max_batch_size) {
					return result;
				}
			}
		}
	}
	return result;
}

void ColdDataCollectorService::submit_to_image_manager(const ColdScanResult& result) {
	if (result.cold_inodes.empty()) {
		return;
	}
	std::shared_ptr<IImageAggregationScheduler> scheduler;
	{
		std::lock_guard<std::mutex> lock(hook_mtx_);
		scheduler = scheduler_;
	}
	if (scheduler) {
		scheduler->schedule_aggregation(result);
		return;
	}
	if (!image_mgr_) {
		LOGW("collector: ImageManager missing, skipped " << result.cold_inodes.size() << " cold inodes");
		return;
	}
	for (const auto& inode : result.inode_records) {
		int rc = image_mgr_->sim_image_write_file(inode);
		if (rc != IMAGE_OP_SUCCESS) {
			LOGW("collector: sim_image_write_file failed for inode " << inode.inode);
		}
	}
}

void ColdDataCollectorService::queue_burn_request(const ColdScanResult& result) {
	if (result.cold_inodes.empty()) {
		return;
	}
	ColdCollectorConfig cfg = snapshot_config();
	std::vector<uint64_t> ids = result.cold_inodes;
	std::thread([cfg, ids = std::move(ids)]() {
		if (cfg.delay_before_burn.count() > 0) {
			std::this_thread::sleep_for(cfg.delay_before_burn);
		}
		LOGI("collector: ready to submit burn IO for " << ids.size() << " cold inodes");
	}).detach();
}

ColdCollectorConfig ColdDataCollectorService::snapshot_config() const {
	std::lock_guard<std::mutex> lock(config_mtx_);
	return config_;
}

void ColdDataCollectorService::buffer_pending_inodes(const ColdScanResult& result) {
	for (const auto& inode : result.inode_records) {
		pending_bytes_ += inode.getFileSize();
		pending_inodes_.push_back(inode);
	}
}

void ColdDataCollectorService::flush_pending_if_needed(bool force) {
	if (pending_inodes_.empty()) {
		return;
	}
	ColdCollectorConfig cfg = snapshot_config();
	if (!force && pending_bytes_ < cfg.image_flush_threshold_bytes) {
		return;
	}
	ColdScanResult aggregate;
	aggregate.collected_at = std::chrono::system_clock::now();
	aggregate.inode_records = pending_inodes_;
	aggregate.cold_inodes.reserve(pending_inodes_.size());
	for (const auto& inode : pending_inodes_) {
		aggregate.cold_inodes.push_back(inode.inode);
	}
	submit_to_image_manager(aggregate);
	queue_burn_request(aggregate);
	pending_inodes_.clear();
	pending_bytes_ = 0;
}

