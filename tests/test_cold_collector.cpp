#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../src/mds/collector/collector.h"
#include "../src/mds/inode/InodeStorage.h"

namespace fs = std::filesystem;

namespace {

constexpr int kYearBase = 2000;

// 将 InodeTimestamp 还原成 std::chrono 时间点，方便做差
std::chrono::system_clock::time_point ts_to_time_point(const InodeTimestamp& ts) {
	std::tm tm{};
	tm.tm_year = kYearBase + static_cast<int>(ts.year) - 1900;
	tm.tm_mon = std::max(0, static_cast<int>(ts.month) - 1);
	tm.tm_mday = std::max(1, static_cast<int>(ts.day));
	tm.tm_hour = static_cast<int>(ts.hour);
	tm.tm_min = static_cast<int>(ts.minute);
	tm.tm_isdst = -1;
	std::time_t tt = std::mktime(&tm);
	return std::chrono::system_clock::from_time_t(tt);
}

// 把 std::chrono 时间点编码到 InodeTimestamp 结构
InodeTimestamp to_timestamp(std::chrono::system_clock::time_point tp) {
	std::time_t tt = std::chrono::system_clock::to_time_t(tp);
	std::tm local_tm = *std::localtime(&tt);
	InodeTimestamp ts;
	int full_year = local_tm.tm_year + 1900;
	int offset = std::clamp(full_year - kYearBase, 0, 255);
	ts.year = static_cast<uint32_t>(offset);
	ts.month = static_cast<uint32_t>(local_tm.tm_mon + 1);
	ts.day = static_cast<uint32_t>(local_tm.tm_mday);
	ts.hour = static_cast<uint32_t>(local_tm.tm_hour);
	ts.minute = static_cast<uint32_t>(local_tm.tm_min);
	return ts;
}

// 根据 inode 号和访问时间生成一个最小可用的 Inode 示例
Inode build_inode(uint64_t ino,
				   const std::string& filename,
				   std::chrono::system_clock::time_point last_access) {
	Inode inode;
	inode.inode = ino;
	inode.setNodeId(1);
	inode.setNodeType(1);
	inode.setBlockId(1);
	inode.setFilename(filename);
	inode.setFileType(static_cast<uint8_t>(FileType::Regular));
	inode.setFilePerm(0644);
	inode.setSizeUnit(0);
	inode.setFileSize(4096);
	inode.setVolumeId("vol-test");
	inode.fa_time = to_timestamp(last_access);
	inode.fm_time = inode.fa_time;
	inode.fc_time = inode.fa_time;
	inode.im_time = inode.fa_time;
	return inode;
}

// 把一批序列化后的 inode 写入指定数据文件
void write_batch_file(const fs::path& file, const std::vector<Inode>& inodes) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		throw std::runtime_error("无法创建测试批文件: " + file.string());
	}
	for (const auto& inode : inodes) {
		auto bytes = inode.serialize();
		if (bytes.size() > InodeStorage::INODE_DISK_SLOT_SIZE) {
			throw std::runtime_error("序列化长度超过槽大小");
		}
		bytes.resize(InodeStorage::INODE_DISK_SLOT_SIZE, 0);
		out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
}

// 直接复用 ColdCollectorConfig 中的时间阈值判定冷数据
class ThresholdSelector : public IColdInodeSelector {
public:
	bool is_cold(const Inode& inode, const ColdCollectorConfig& cfg) const override {
		auto now = std::chrono::system_clock::now();
		auto access_tp = ts_to_time_point(inode.fa_time);
		return (now - access_tp) >= cfg.cold_threshold;
	}
};

} // namespace

int run_real_scan(const fs::path& source_path, size_t max_inodes) {
	std::error_code ec;
	fs::path work_dir;
	std::optional<fs::path> cleanup_dir;
	if (fs::is_regular_file(source_path, ec)) {
		work_dir = fs::temp_directory_path() / "zb_collector_link";
		fs::remove_all(work_dir, ec);
		fs::create_directories(work_dir, ec);
		if (ec) {
			std::cerr << "创建工作目录失败: " << ec.message() << std::endl;
			return 1;
		}
		fs::path link_path = work_dir / source_path.filename();
		fs::remove(link_path, ec);
		fs::create_hard_link(source_path, link_path, ec);
		if (ec) {
			// 某些跨设备场景不支持硬链接，退化到复制
			ec.clear();
			fs::copy_file(source_path, link_path, fs::copy_options::overwrite_existing, ec);
			if (ec) {
				std::cerr << "复制文件失败: " << ec.message() << std::endl;
				return 1;
			}
		}
		cleanup_dir = work_dir;
	} else if (fs::is_directory(source_path, ec)) {
		work_dir = source_path;
	} else {
		std::cerr << "路径无效: " << source_path << std::endl;
		return 1;
	}

	ColdCollectorConfig cfg;
	cfg.inode_directory = work_dir.string();
	cfg.scan_interval = std::chrono::hours(1);
	cfg.cold_threshold = std::chrono::hours(24 * 180);
	cfg.max_inodes_per_round = max_inodes;
	cfg.max_batch_size = max_inodes;
	cfg.delay_before_burn = std::chrono::seconds(0);

	ColdDataCollectorService service(nullptr, nullptr, cfg);
	ColdScanResult result = service.run_single_scan_for_test();
	std::cout << "扫描路径: " << source_path << std::endl;
	std::cout << "检查 inode 数: " << cfg.max_inodes_per_round
	          << " 识别冷 inode 数: " << result.cold_inodes.size() << std::endl;
	for (size_t i = 0; i < std::min<size_t>(result.inode_records.size(), 5); ++i) {
		const auto& inode = result.inode_records[i];
		std::cout << "  [" << i << "] ino=" << inode.inode
		          << " name=" << inode.filename
		          << " last_access_year=" << static_cast<int>(inode.fa_time.year + kYearBase)
		          << std::endl;
	}
	if (cleanup_dir) {
		fs::remove_all(*cleanup_dir, ec);
	}
	return 0;
}

int run_directory_sequence(const fs::path& dir,
					 size_t start_idx,
					 size_t end_idx,
					 size_t max_inodes,
					 std::ostream& log) {
	if (!fs::exists(dir)) {
		log << "目录不存在: " << dir << std::endl;
		return 1;
	}
	int rc = 0;
	for (size_t idx = start_idx; idx <= end_idx; ++idx) {
		std::ostringstream oss;
		oss << "inode_chunk_" << idx << ".bin";
		fs::path file = dir / oss.str();
		if (!fs::exists(file)) {
			log << "跳过缺失文件: " << file << std::endl;
			continue;
		}
		log << "========== 扫描批次 " << idx << " ==========" << std::endl;
		rc = run_real_scan(file, max_inodes);
		if (rc != 0) {
			log << "批次 " << idx << " 扫描失败，提前退出" << std::endl;
			return rc;
		}
	}
	return rc;
}

int main(int argc, char** argv) {
	if (argc > 1) {
		fs::path path = argv[1];
		size_t max_inodes = 10'000;
		if (argc > 2) {
			max_inodes = static_cast<size_t>(std::stoull(argv[2]));
		}
		return run_real_scan(path, max_inodes);
	}

	const fs::path default_dir = "/mnt/md0/inode";
	if (fs::exists(default_dir / "inode_chunk_0.bin")) {
		fs::path log_path = fs::current_path() / "collector_scan.log";
		std::ofstream log_file(log_path, std::ios::app);
		if (!log_file) {
			std::cerr << "无法打开日志文件: " << log_path << std::endl;
			return 1;
		}
		log_file << "\n==== 执行时间: " << std::time(nullptr) << " ====\n";
		log_file << "未提供参数，默认扫描目录: " << default_dir
		         << " 范围 inode_chunk_0.bin ~ inode_chunk_999.bin" << std::endl;
		return run_directory_sequence(default_dir, 0, 999, 100'000, log_file);
	}

	const fs::path temp_dir = fs::temp_directory_path() / "zb_collector_test";
	std::error_code ec;
	fs::remove_all(temp_dir, ec);
	fs::create_directories(temp_dir, ec);
	if (ec) {
		std::cerr << "创建临时目录失败: " << ec.message() << std::endl;
		return 1;
	}

	auto now = std::chrono::system_clock::now();
	Inode hot = build_inode(1001, "hot.bin", now - std::chrono::hours(24));
	Inode cold = build_inode(1002, "cold.bin", now - std::chrono::hours(24 * 365));

	try {
		write_batch_file(temp_dir / "batch.bin", {hot, cold});
	} catch (const std::exception& ex) {
		std::cerr << "写入批文件失败: " << ex.what() << std::endl;
		return 1;
	}

	ColdCollectorConfig cfg;
	cfg.inode_directory = temp_dir.string();
	cfg.scan_interval = std::chrono::hours(1);
	cfg.cold_threshold = std::chrono::hours(24 * 180);
	cfg.max_inodes_per_round = 16;
	cfg.max_batch_size = 16;
	cfg.delay_before_burn = std::chrono::seconds(0);
	cfg.image_flush_threshold_bytes = 0; // 测试场景希望立即触发聚合
	std::cout << "配置扫描目录: " << cfg.inode_directory
	          << " 冷阈值(小时): " << std::chrono::duration_cast<std::chrono::hours>(cfg.cold_threshold).count()
	          << std::endl;

	ColdDataCollectorService service(nullptr, nullptr, cfg);
	service.set_selector(std::make_shared<ThresholdSelector>());
	std::cout << "启动前准备完成，执行 run_single_scan_for_test()" << std::endl;

	ColdScanResult result = service.run_single_scan_for_test();
	std::cout << "扫描结束，收集到冷 inode 数: " << result.cold_inodes.size() << std::endl;
	if (result.cold_inodes.size() != 1) {
		std::cerr << "期望 1 个冷 inode, 实际 " << result.cold_inodes.size() << std::endl;
		return 1;
	}
	if (result.cold_inodes.front() != cold.inode) {
		std::cerr << "冷 inode 号不匹配, 期望 " << cold.inode
			  << " 实际 " << result.cold_inodes.front() << std::endl;
		return 1;
	}

	std::cout << "ColdDataCollectorService 单次扫描测试通过, 冷 inode 数: "
		      << result.cold_inodes.size() << std::endl;
	return 0;
}
