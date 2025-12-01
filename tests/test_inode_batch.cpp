#include <algorithm>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "../src/mds/inode/InodeStorage.h"

namespace fs = std::filesystem;

namespace {

constexpr uint64_t TOTAL_INODES = 1'000'000'000ULL;     // 10^9
constexpr size_t   INODES_PER_FILE = 1'000'000;         // 每百万一个文件

enum class BatchTempClass { Hot = 0, Warm = 1, Cold = 2 };

BatchTempClass pick_batch_temp(size_t batch_idx) {
    return static_cast<BatchTempClass>(batch_idx % 3);
}

void apply_temperature_ratio(InodeStorage::BatchGenerationConfig& cfg, BatchTempClass klass) {
    switch (klass) {
        case BatchTempClass::Hot:
            cfg.temp_ratio = {1.0, 0.0, 0.0};
            break;
        case BatchTempClass::Warm:
            cfg.temp_ratio = {0.0, 1.0, 0.0};
            break;
        case BatchTempClass::Cold:
        default:
            cfg.temp_ratio = {0.0, 0.0, 1.0};
            break;
    }
}

uint8_t infer_node_type(BatchTempClass klass) {
    switch (klass) {
        case BatchTempClass::Hot: return 0; // SSD
        case BatchTempClass::Warm: return 1; // HDD
        case BatchTempClass::Cold:
        default: return 2; // Mix
    }
}

std::string build_root_path(size_t batch_idx) {
    std::ostringstream oss;
    oss << "/dataset/batch_" << batch_idx;
    return oss.str();
}

std::string build_output_path(const fs::path& base_dir, size_t batch_idx) {
    std::ostringstream oss;
    oss << "inode_chunk_" << batch_idx << ".bin";
    return (base_dir / oss.str()).string();
}

uint16_t pick_node_id(size_t batch_idx) {
    constexpr uint16_t MAX_NODE_ID = 10'000;
    uint16_t id = static_cast<uint16_t>((batch_idx % MAX_NODE_ID) + 1);
    return id;
}

struct BatchStats {
    uint64_t hot = 0;
    uint64_t warm = 0;
    uint64_t cold = 0;
};

bool write_one_batch(const fs::path& out_dir,
                     size_t batch_idx,
                     uint64_t starting_inode,
                     size_t batch_size,
                     BatchStats& stats) {
    InodeStorage::BatchGenerationConfig cfg;
    cfg.batch_size = batch_size;
    cfg.starting_inode = starting_inode;
    cfg.output_file = build_output_path(out_dir, batch_idx);
    cfg.random_seed = static_cast<uint32_t>(batch_idx + 12345);
    cfg.verbose = false;
    cfg.dir_depth = 4;    // 同一目录树
    cfg.dir_fanout = 8;
    cfg.root_path = build_root_path(batch_idx);

    BatchTempClass klass = pick_batch_temp(batch_idx);
    apply_temperature_ratio(cfg, klass);

    uint16_t node_id = pick_node_id(batch_idx);
    cfg.node_distribution = {
        {node_id, infer_node_type(klass), 1.0}
    };

    try {
        if (!InodeStorage::generate_metadata_batch(cfg)) {
            std::cerr << "批次 " << batch_idx << " 生成失败" << std::endl;
            return false;
        }
    } catch (const std::exception& ex) {
        std::cerr << "批次 " << batch_idx << " 异常: " << ex.what() << std::endl;
        return false;
    }

    const uint64_t expected_size = static_cast<uint64_t>(batch_size) * InodeStorage::INODE_DISK_SLOT_SIZE;
    std::error_code ec;
    uint64_t actual = fs::file_size(cfg.output_file, ec);
    if (ec) {
        std::cerr << "无法获取文件大小: " << cfg.output_file << " 错误: " << ec.message() << std::endl;
        return false;
    }
    if (actual != expected_size) {
        std::cerr << "文件大小不符, 批次 " << batch_idx
                  << " 期望 " << expected_size << " 实际 " << actual << std::endl;
        return false;
    }

    switch (klass) {
        case BatchTempClass::Hot: stats.hot += batch_size; break;
        case BatchTempClass::Warm: stats.warm += batch_size; break;
        case BatchTempClass::Cold:
        default: stats.cold += batch_size; break;
    }

    std::cout << "[Batch] 完成 index=" << batch_idx
              << " inode_range=[" << starting_inode << ", " << (starting_inode + batch_size - 1) << "]"
              << " temp=" << static_cast<int>(klass)
              << " node=" << node_id
              << " file=" << cfg.output_file << std::endl;
    return true;
}

} // namespace

int main() {
    const fs::path output_dir = "/mnt/md0/inode";
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        std::cerr << "创建输出目录失败: " << ec.message() << std::endl;
        return 1;
    }

    const uint64_t total_inodes = TOTAL_INODES;
    const size_t batch_size = INODES_PER_FILE;
    const uint64_t total_batches = (total_inodes + batch_size - 1) / batch_size;

    uint64_t generated = 0;
    BatchStats stats;
    auto start_time = std::chrono::steady_clock::now();
    for (uint64_t batch = 0; batch < total_batches; ++batch) {
        std::cout << "[Batch] 开始生成第 " << batch + 1 << "/" << total_batches << " 个文件" << std::endl;
        size_t current_batch_size = static_cast<size_t>(std::min<uint64_t>(batch_size, total_inodes - generated));
        if (current_batch_size == 0) break;

        auto batch_start = std::chrono::steady_clock::now();

        if (!write_one_batch(output_dir, batch, generated, current_batch_size, stats)) {
            std::cerr << "批次 " << batch << " 失败，终止。" << std::endl;
            return 1;
        }

        auto batch_end = std::chrono::steady_clock::now();
        auto batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(batch_end - batch_start).count();

        generated += current_batch_size;
        std::cout << "[Batch] 完成第 " << batch + 1 << " 个文件，用时 " << batch_ms << " ms"
                  << "，累计 inode: " << generated << "/" << total_inodes << std::endl;
    }

    if (generated != total_inodes) {
        std::cerr << "生成数量不符，期望 " << total_inodes
                  << " 实际 " << generated << std::endl;
        return 1;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "全部完成，共 " << total_batches << " 个文件，每个最多 "
              << batch_size << " 条，总耗时 " << total_ms / 1000.0 << " s" << std::endl;
    std::cout << "统计: Hot=" << stats.hot
              << " Warm=" << stats.warm
              << " Cold=" << stats.cold << std::endl;
    return 0;
}
