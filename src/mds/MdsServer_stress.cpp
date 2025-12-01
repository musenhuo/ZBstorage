#include "server/Server.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

struct Params {
    size_t depth = 3;
    size_t fanout = 10;
    size_t files_per_dir = 10;
    size_t query_count = 1000;
    bool reuse_existing = false;
    bool enable_inode_cache = true;
    std::string store_base = "/mnt/nvme/node";
    uint64_t random_seed = std::random_device{}();
};

/**
 * @brief 根据层级和序号生成目录名（形如 dL_IDX）。 
 * @param level 当前层级，从 1 开始。
 * @param idx 当前层级下的子目录序号。
 * @return 目录名字符串。
 */
std::string make_dir_name(size_t level, size_t idx) {
    return "d" + std::to_string(level) + "_" + std::to_string(idx);
}

/**
 * @brief 根据序号生成文件名（形如 f_IDX）。
 * @param idx 文件序号。
 * @return 文件名字符串。
 */
std::string make_file_name(size_t idx) {
    return "f_" + std::to_string(idx);
}

/**
 * @brief 随机生成一条文件路径，用于查询阶段。
 * @param depth 树深度。
 * @param fanout 每层分支数。
 * @param files_per_dir 每个叶目录的文件数量；若为 0 则返回目录路径。
 * @param rng 随机数引擎。
 * @return 生成的绝对路径。
 */
std::string compose_random_path(size_t depth,
                                size_t fanout,
                                size_t files_per_dir,
                                std::mt19937_64& rng) {
    if (depth == 0) return "/";
    std::uniform_int_distribution<size_t> dir_dist(0, fanout > 0 ? fanout - 1 : 0);
    std::uniform_int_distribution<size_t> file_dist(0, files_per_dir > 0 ? files_per_dir - 1 : 0);

    std::string path = "/";
    for (size_t lvl = 1; lvl <= depth; ++lvl) {
        size_t idx = dir_dist(rng);
        std::string name = make_dir_name(lvl, idx);
        if (path.size() > 1) path += "/";
        path += name;
    }
    if (files_per_dir > 0) {
        size_t file_idx = file_dist(rng);
        path += "/";
        path += make_file_name(file_idx);
    }
    return path;
}

/**
 * @brief 解析命令行参数，构造压力测试配置。
 * @param argc main 的参数数量。
 * @param argv main 的参数数组。
 * @return 填充后的 Params。
 */
Params parse_args(int argc, char** argv) {
    Params params;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto consume = [&](const std::string& prefix, auto setter) {
            if (arg.rfind(prefix, 0) == 0) {
                setter(arg.substr(prefix.size()));
                return true;
            }
            return false;
        };
        if (consume("--depth=", [&](const std::string& v) { params.depth = std::stoull(v); })) continue;
        if (consume("--fanout=", [&](const std::string& v) { params.fanout = std::stoull(v); })) continue;
        if (consume("--files=", [&](const std::string& v) { params.files_per_dir = std::stoull(v); })) continue;
        if (consume("--queries=", [&](const std::string& v) { params.query_count = std::stoull(v); })) continue;
        if (consume("--base=", [&](const std::string& v) { params.store_base = v; })) continue;
        if (consume("--seed=", [&](const std::string& v) { params.random_seed = std::stoull(v); })) continue;
        if (arg == "--reuse") { params.reuse_existing = true; continue; }
        if (arg == "--no-cache") { params.enable_inode_cache = false; continue; }
        if (arg == "--cache") { params.enable_inode_cache = true; continue; }
        std::cerr << "[WARN] 未识别的参数: " << arg << std::endl;
    }
    return params;
}

// 用例：./mds_server_stress --depth=5 --fanout=20 --files=50 --queries=10000

} // namespace

/**
 * @brief 程序入口：执行 MDS 规模化创建与随机查询压力测试。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 0 表成功，其它为失败。
 */
int main(int argc, char** argv) {
    Params params = parse_args(argc, argv);

    if (params.fanout == 0) {
        std::cerr << "[ERROR] fanout 不能为 0。" << std::endl;
        return 1;
    }
    if (params.depth == 0) {
        std::cerr << "[ERROR] depth 需大于 0。" << std::endl;
        return 1;
    }
    if (params.files_per_dir == 0) {
        std::cerr << "[WARN] files_per_dir 为 0，随机解析仅验证目录节点。" << std::endl;
    }

    std::filesystem::path base = params.store_base;
    std::string inode_path = (base / "inodes.bin").string();
    std::string bitmap_path = (base / "bitmap.bin").string();
    std::string dir_store_path = (base / "dir_store").string();

    if (!params.reuse_existing) {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        std::filesystem::create_directories(base, ec);
        std::filesystem::create_directories(dir_store_path, ec);
    } else {
        std::filesystem::create_directories(base);
        std::filesystem::create_directories(dir_store_path);
    }

    MdsServer mds(inode_path, bitmap_path, dir_store_path, !params.reuse_existing);
    if (!params.reuse_existing) {
        if (!mds.CreateRoot()) {
            std::cerr << "[ERROR] CreateRoot 失败。" << std::endl;
            return 1;
        }
    } else {
        if (params.enable_inode_cache) {
            mds.RebuildInodeTable();
        } else {
            std::cout << "[INFO] reuse_existing=true 且禁用 inode 缓存，跳过 RebuildInodeTable。" << std::endl;
        }
    }

    std::mt19937_64 creation_rng(params.random_seed);
    std::uniform_int_distribution<int> size_dist(30, 70);
    std::uniform_int_distribution<int> storage_node_dist(1, 1000);

    if (!params.reuse_existing) {
        size_t total_dirs = 0;
        size_t total_files = 0;

        auto t_creation_start = std::chrono::steady_clock::now();

        std::function<void(size_t, const std::string&)> create_level;
        create_level = [&](size_t level, const std::string& parent_path) {
            if (level > params.depth) {
                for (size_t f = 0; f < params.files_per_dir; ++f) {
                    std::string file_name = make_file_name(f);
                    std::string file_path = parent_path == "/"
                        ? "/" + file_name
                        : parent_path + "/" + file_name;
                    if (!mds.CreateFile(file_path, 0644)) {
                        std::cerr << "[ERROR] CreateFile 失败: " << file_path << std::endl;
                        std::exit(1);
                    }

                    uint64_t ino = mds.LookupIno(file_path);
                    if (ino != static_cast<uint64_t>(-1)) {
                        Inode inode;
                        if (mds.ReadInode(ino, inode)) {
                            uint16_t size_mb = static_cast<uint16_t>(size_dist(creation_rng));
                            uint16_t storage_id = static_cast<uint16_t>(storage_node_dist(creation_rng));

                            inode.setSizeUnit(2);          // 2 -> MB
                            inode.setFileSize(size_mb);
                            inode.setNodeId(storage_id);
                            inode.setNodeType(0);

                            if (!mds.WriteInode(ino, inode)) {
                                std::cerr << "[WARN] 写回 inode 失败: " << file_path << std::endl;
                            }
                        } else {
                            std::cerr << "[WARN] 读取 inode 失败: " << file_path << std::endl;
                        }
                    } else {
                        std::cerr << "[WARN] LookupIno 失败: " << file_path << std::endl;
                    }

                    ++total_files;
                }
                return;
            }

            for (size_t i = 0; i < params.fanout; ++i) {
                std::string dir_name = make_dir_name(level, i);
                std::string dir_path = parent_path == "/"
                    ? "/" + dir_name
                    : parent_path + "/" + dir_name;
                if (!mds.Mkdir(dir_path, 0755)) {
                    std::cerr << "[ERROR] Mkdir 失败: " << dir_path << std::endl;
                    std::exit(1);
                }
                ++total_dirs;
                create_level(level + 1, dir_path);
            }
        };

        create_level(1, "/");

        auto t_creation_end = std::chrono::steady_clock::now();
        auto creation_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_creation_end - t_creation_start).count();

        double avg_op_ns = (total_dirs + total_files) > 0
            ? static_cast<double>(creation_ns) / static_cast<double>(total_dirs + total_files)
            : 0.0;

        std::cout << "[STATS] 创建完成: 目录 " << total_dirs
                  << " 个，文件 " << total_files
                  << " 个，总耗时 " << creation_ns / 1e9 << " s" << std::endl;
        std::cout << "[STATS] 平均每次（目录/文件）创建耗时 "
                  << avg_op_ns / 1e6 << " ms" << std::endl;

        if (!params.enable_inode_cache) {
            mds.ClearInodeTable();
            std::cout << "[INFO] 已清空 inode_table，后续查询将走逐层解析路径。" << std::endl;
        }
    } else {
        if (!params.enable_inode_cache) {
            std::cout << "[INFO] reuse_existing=true，跳过目录/文件创建且不构建 inode_table。" << std::endl;
        } else {
            std::cout << "[INFO] reuse_existing=true，使用现有数据并已重建 inode_table。" << std::endl;
        }
    }

    std::mt19937_64 query_rng(params.random_seed ^ 0x9e3779b97f4a7c15ULL);

    auto t_queries_start = std::chrono::steady_clock::now();
    size_t success = 0;

    for (size_t q = 0; q < params.query_count; ++q) {
        std::string path = compose_random_path(params.depth,
                                               params.fanout,
                                               params.files_per_dir,
                                               query_rng);
        auto path_start = std::chrono::steady_clock::now();
        auto inode = mds.FindInodeByPath(path);
        auto path_end = std::chrono::steady_clock::now();
        if (inode) ++success;

        auto single_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(path_end - path_start).count();
        (void)single_ns; // 若需 per-query 耗时，可在此收集
    }

    auto t_queries_end = std::chrono::steady_clock::now();
    auto queries_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_queries_end - t_queries_start).count();
    double avg_query_ns = params.query_count > 0
        ? static_cast<double>(queries_ns) / static_cast<double>(params.query_count)
        : 0.0;

    std::cout << "[STATS] 路径解析: 成功 " << success << "/" << params.query_count
              << "，总耗时 " << queries_ns / 1e9 << " s" << std::endl;
    std::cout << "[STATS] 平均每次解析耗时 " << avg_query_ns / 1e6 << " ms" << std::endl;

    std::cout << "[INFO] 压力测试结束。" << std::endl;
    return 0;
}