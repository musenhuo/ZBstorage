// VFS.cpp: FileSystem 相关函数定义
#include "VFS.h"

#include <algorithm>
#include <iostream>

// @wbl 创建根目录
bool FileSystem::create_root_directory(){
    const std::string root_path = "/";
    if (inode_table.find(root_path) != inode_table.end()) {
        // 根目录已存在
        return true;
    }

    auto inode = std::make_shared<Inode>();
    inode->setFilename(root_path);
    inode->setFileType(static_cast<uint8_t>(FileType::Directory));
    inode->setFilePerm(0755); // rwxr-xr-x
    inode->setSizeUnit(0);
    inode->setFileSize(0);
    InodeTimestamp now;
    inode->setFmTime(now);
    inode->setFaTime(now);
    inode->setFcTime(now);

    // 分配inode号
    uint64_t ino = metadata_manager->allocate_inode(inode->file_mode.raw);
    if (ino == static_cast<uint64_t>(-1)) {
        std::cerr << "[CREATE_ROOT] 无法分配inode号" << std::endl;
        return false;
    }
    inode->inode = ino;
    LOGD("[CREATE_ROOT] 分配根目录 inode 号: " << ino);
    // std::cout << "[CREATE_ROOT] 分配根目录 inode 号: " << ino << std::endl;

    try {
        // 分配卷空间
        alloc_volume_for_inode(inode);
        if (inode->volume_id.empty()) {
            std::cerr << "[CREATE_ROOT] 无法为根目录分配卷空间" << std::endl;
            return false;
        }
        
        // 构造初始目录内容
        DirectoryEntry self_entry(".", inode->inode, FileType::Directory);
        DirectoryEntry parent_entry("..", inode->inode, FileType::Directory);

        // std::cout << "[CREATE_ROOT] 根目录初始目录项准备完成" << std::endl;

        // 写入目录项
        if (!addDirectoryEntry(inode, self_entry) || 
            !addDirectoryEntry(inode, parent_entry)) {
            // 回滚：释放已分配的资源
            // 需要根据你的实现添加相应的清理代码
            return false;
        }
        // std::cout << "[CREATE_ROOT] 根目录初始目录项写入完成" << std::endl;

    } catch (...) {
        // 异常处理：释放已分配的资源
        return false;
    }

    // 保存inode到存储
    if (!metadata_manager->get_inode_storage()->write_inode(ino, *inode)) {
        std::cerr << "[CREATE_ROOT] 无法保存根目录inode" << std::endl;
        return false;
    }

    // 更新内存表
    inode_table[root_path] = ino;
    LOGD("[CREATE_ROOT] 根目录创建成功");
    // std::cout << "[CREATE_ROOT] 根目录创建成功" << std::endl;
    return true;
}

FileSystem::FileSystem(bool create_new, int fd_bitmap_size) :fd_bitmap(fd_bitmap_size, false) {
    // 可以根据需要初始化其他成员
    // 例如：清空 inode_table 和 fd_table
    inode_table.clear();
    fd_bitmap.flip();  // 关键操作：翻转所有位（0→1）
    // 标准FD 0,1,2设为占用（0）
    // std::cout << "fd_bitmap: ";
    // for (size_t i = 0; i < 10; ++i) {
    //     std::cout << fd_bitmap[i] << "";
    // }
    // std::cout << std::endl;
    for(int fd : {0, 1, 2}) {
        if(fd < fd_bitmap.size()) 
            fd_bitmap.reset(fd);
    }
    metadata_manager = std::make_unique<MetadataManager>(
    INODE_STORAGE_PATH, INODE_BITMAP_PATH, create_new); // ！!true和false需要进一步明确

    // 初始化卷持久化元数据文件与内存计数器
    FileSystem::ensure_meta_initialized(SSD_VOLUME_META_PATH);
    FileSystem::ensure_meta_initialized(HDD_VOLUME_META_PATH);
    int ssd_cnt = get_volume_count_core(SSD_VOLUME_META_PATH);
    int hdd_cnt = get_volume_count_core(HDD_VOLUME_META_PATH);
    ssd_next_index = static_cast<uint32_t>(std::max(0, ssd_cnt));
    hdd_next_index = static_cast<uint32_t>(std::max(0, hdd_cnt));

    // 创建根目录
    // create_root_directory();
}

// demo 未融入主流程
void FileSystem::start_access_collector(std::chrono::milliseconds period,
                                        size_t window_count,
                                        size_t bits_per_filter,
                                        size_t hash_count) {
    stop_access_collector();

    access_period = period;
    access_window_count = std::max<size_t>(1, window_count);
    access_bits_per_filter = bits_per_filter; // bits
    access_hash_count = std::max<size_t>(1, hash_count);

    // 创建 tracker（后续可替换）
    access_tracker = std::make_unique<BloomAccessTracker>(access_window_count, access_bits_per_filter, access_hash_count);

    access_collector_running.store(true);
    access_collector_thread = std::thread([this]() {
        while (access_collector_running.load()) {
            // sleep for period or until stop signaled
            std::unique_lock<std::mutex> lk(this->access_collector_mtx);
            this->access_collector_cv.wait_for(lk, access_period, [this]() { return !access_collector_running.load(); });
            if (!access_collector_running.load()) break;
            // rotate window (clear oldest)
            if (access_tracker) access_tracker->rotate();
        }
    });
}

void FileSystem::stop_access_collector() {
    access_collector_running.store(false);
    access_collector_cv.notify_all();
    if (access_collector_thread.joinable()) access_collector_thread.join();
    // keep tracker data unless reset desired
}

std::vector<uint64_t> FileSystem::collect_cold_inodes(size_t max_candidates, size_t min_age_windows) {
    std::vector<uint64_t> result;
    if (!metadata_manager || !access_tracker) return result;

    // 避免一次遍历过多，按增量游标扫描已分配 inode
    auto inode_storage = metadata_manager->get_inode_storage();
    uint64_t total_slots = metadata_manager->get_total_inodes();
    if (total_slots == 0) return result;

    uint64_t start;
    {
        std::lock_guard<std::mutex> lk(access_scan_mtx);
        start = access_scan_cursor % total_slots;
    }

    uint64_t scanned = 0;
    uint64_t cur = start;
    while (scanned < total_slots && result.size() < max_candidates) {
        // 先判断是否已分配，避免对空槽做 I/O
        if (metadata_manager->is_inode_allocated(cur)) {
            // 只查询布隆，不触发磁盘I/O
            bool hot = access_tracker->possibly_hot(cur, min_age_windows);
            if (!hot) {
                result.push_back(cur);
            }
        }
        ++cur;
        if (cur >= total_slots) cur = 0;
        ++scanned;
    }

    {
        std::lock_guard<std::mutex> lk(access_scan_mtx);
        access_scan_cursor = cur;
    }
    return result;
}

std::shared_ptr<boost::dynamic_bitset<>> FileSystem::collect_cold_inodes_bitmap(size_t min_age_windows) {
    if (!metadata_manager || !access_tracker) return nullptr;

    uint64_t total_slots = metadata_manager->get_total_inodes();
    if (total_slots == 0) return nullptr;

    // 分配位图（所有位初始为0）
    auto cold_bitmap = std::make_shared<boost::dynamic_bitset<>>(static_cast<size_t>(total_slots));

    // 使用游标分批扫描以降低单次峰值开销（可根据需要改成一次性全量扫描）
    uint64_t start;
    {
        std::lock_guard<std::mutex> lk(access_scan_mtx);
        start = access_scan_cursor % total_slots;
    }

    // 我们做一次全轮扫描以生成完整位图；若需要减小瞬时开销，可分多次增量生成并累积结果
    uint64_t cur = 0;
    for (uint64_t i = 0; i < total_slots; ++i) {
        cur = (start + i);
        if (cur >= total_slots) cur -= total_slots;

        // 先判断是否已分配，避免读取空槽
        if (!metadata_manager->is_inode_allocated(cur)) continue;

        // 只查询访问追踪器，不触发磁盘 I/O
        bool hot = access_tracker->possibly_hot(cur, min_age_windows);
        if (!hot) {
            cold_bitmap->set(static_cast<size_t>(cur)); // 标记为冷
        }
    }

    // 更新游标（下次扫描可以从这里继续，用于增量化）
    {
        std::lock_guard<std::mutex> lk(access_scan_mtx);
        access_scan_cursor = (start + total_slots) % total_slots;
    }

    return cold_bitmap;
}

// 简单扫盘逻辑的实现
// helper: 把 InodeTimestamp 映射为可比较的整数键（越小表示越早）
// 这里采用按位组合的方法，避免依赖时区或 epoch 解析。
// 假设 InodeTimestamp 的 year/month/day/hour/minute 都是按自然数表示。
static uint32_t inode_timestamp_key(const InodeTimestamp& t) {
    // year: 8 bits, month 6 bits, day 6 bits, hour 6 bits, minute 6 bits -> 合计 32 bits（实际字段已保证）
    // 组合顺序从高到低：year|month|day|hour|minute
    uint32_t key = 0;
    key |= (static_cast<uint32_t>(t.year) & 0xFF) << 24;         // top 8 bits
    key |= (static_cast<uint32_t>(t.month) & 0x3F) << 18;        // next 6 bits
    key |= (static_cast<uint32_t>(t.day) & 0x3F) << 12;          // next 6 bits
    key |= (static_cast<uint32_t>(t.hour) & 0x3F) << 6;          // next 6 bits
    key |= (static_cast<uint32_t>(t.minute) & 0x3F);             // last 6 bits
    return key;
}

// collect_cold_inodes_by_atime_percent:
// 扫描 metadata_manager 中记录的所有 inode 槽（通过 total_inodes），
// 对已分配 inode 读取其 Inode（read_inode），按照 fa_time 的键排序，
// 返回最老的前 percent% 的 inode id 列表。
// percent 范围 0.0 - 100.0，若 percent<=0 返回空，>=100 返回所有已分配 inode。
std::vector<uint64_t> FileSystem::collect_cold_inodes_by_atime_percent(double percent) {
    std::vector<uint64_t> result;
    if (!metadata_manager) return result;
    if (percent <= 0.0) return result;

    uint64_t total_slots = metadata_manager->get_total_inodes();
    if (total_slots == 0) return result;

    // 收集 (ino, time_key)
    std::vector<std::pair<uint64_t, uint32_t>> vec;
    vec.reserve(1024);

    for (uint64_t ino = 0; ino < total_slots; ++ino) {
        // 避免读空槽：先由位图判断
        if (!metadata_manager->is_inode_allocated(ino)) continue;
        Inode dinode;
        if (!metadata_manager->get_inode_storage()->read_inode(ino, dinode)) {
            // 读失败，跳过
            continue;
        }

        // debug用：输出 inode 号 与 最后访问时间（fa_time），便于验证
        LOGD("[ATIME] ino=" << ino << " fa_time=");
        // std::cout << "[ATIME] ino=" << ino << " fa_time=";
        dinode.fa_time.print();

        uint32_t key = inode_timestamp_key(dinode.fa_time);
        vec.emplace_back(ino, key);
    }

    if (vec.empty()) return result;

    // 按 time_key 升序（越早越小），stable_sort 保持稳定
    std::stable_sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    // 计算选取数量
    size_t total = vec.size();
    size_t pick = static_cast<size_t>(std::ceil((percent / 100.0) * static_cast<double>(total)));
    if (pick == 0 && percent > 0.0) pick = 1; // 至少取1个
    if (pick > total) pick = total;

    result.reserve(pick);
    for (size_t i = 0; i < pick; ++i) result.push_back(vec[i].first);
    return result;
}

// ============ 辅助：meta 初始化/读写（前缀和数组） ============

// meta 格式（小端）：
// [0..3]  uint32_t count
// [4..]   uint64_t prefix[0..count-1]   // 第 i 项表示前 i+1 个块长度的前缀和（字节）
// data 格式：拼接的可变长度块，块 i 的起始偏移 = (i==0 ? 0 : prefix[i-1])，长度 = prefix[i] - (i==0?0:prefix[i-1])

bool FileSystem::ensure_meta_initialized(const std::string& meta_filename) {
    std::ifstream ifs(meta_filename, std::ios::binary);
    if (ifs.good()) {
        return true; // 已存在，直接使用
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(meta_filename).parent_path(), ec);

    std::ofstream ofs(meta_filename, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    uint32_t zero = 0;
    ofs.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    return static_cast<bool>(ofs);
}

int FileSystem::get_volume_count_core(const std::string& meta_filename) const {
    std::ifstream ifs(meta_filename, std::ios::binary);
    if (!ifs) return 0;
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!ifs) return 0;
    return static_cast<int>(count);
}

bool FileSystem::read_meta_last_prefix(const std::string& meta_filename,
                                       uint32_t& out_count,
                                       uint64_t& out_last_prefix) {
    std::ifstream ifs(meta_filename, std::ios::binary);
    if (!ifs) return false;
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!ifs) return false;
    out_count = count;
    if (count == 0) { out_last_prefix = 0; return true; }
    std::streamoff pos = static_cast<std::streamoff>(sizeof(uint32_t) + sizeof(uint64_t) * (count - 1));
    ifs.seekg(pos, std::ios::beg);
    if (!ifs) return false;
    ifs.read(reinterpret_cast<char*>(&out_last_prefix), sizeof(uint64_t));
    return static_cast<bool>(ifs);
}

bool FileSystem::append_meta_prefix(const std::string& meta_filename,
                                    uint64_t new_prefix,
                                    uint32_t new_count) {
    // 先在尾部追加前缀
    std::fstream fs(meta_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs) return false;
    fs.seekp(0, std::ios::end);
    if (!fs) return false;
    fs.write(reinterpret_cast<const char*>(&new_prefix), sizeof(uint64_t));
    if (!fs) return false;

    // 再回到文件头更新 count
    fs.seekp(0, std::ios::beg);
    if (!fs) return false;
    fs.write(reinterpret_cast<const char*>(&new_count), sizeof(uint32_t));
    fs.flush();
    return static_cast<bool>(fs);
}

bool FileSystem::read_meta_prefix_pair(const std::string& meta_filename,
                                       uint32_t index,
                                       uint32_t& out_count,
                                       uint64_t& out_prev_prefix,
                                       uint64_t& out_cur_prefix) {
    std::ifstream ifs(meta_filename, std::ios::binary);
    if (!ifs) return false;
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!ifs || index >= count) return false;
    // 读取 prefix[index-1] 与 prefix[index]
    if (index == 0) {
        out_prev_prefix = 0;
    } else {
        std::streamoff p_prev = static_cast<std::streamoff>(sizeof(uint32_t) + sizeof(uint64_t) * (index - 1));
        ifs.seekg(p_prev, std::ios::beg);
        if (!ifs) return false;
        ifs.read(reinterpret_cast<char*>(&out_prev_prefix), sizeof(uint64_t));
        if (!ifs) return false;
    }
    std::streamoff p_cur = static_cast<std::streamoff>(sizeof(uint32_t) + sizeof(uint64_t) * index);
    ifs.seekg(p_cur, std::ios::beg);
    if (!ifs) return false;
    ifs.read(reinterpret_cast<char*>(&out_cur_prefix), sizeof(uint64_t));
    if (!ifs) return false;

    out_count = count;
    return true;
}

// ============ 加载第 n 个卷（meta+data） ============

bool FileSystem::load_nth_volume_core(const std::string& meta_filename,
                                       const std::string& data_filename,
                                       uint32_t index,
                                       std::vector<std::shared_ptr<Volume>>& out_list) {
    if (!ensure_meta_initialized(meta_filename)) {
        std::cerr << "[Volume] 初始化 meta 失败: " << meta_filename << std::endl;
        return false;
    }

    uint32_t count = 0;
    uint64_t prev_prefix = 0, cur_prefix = 0;
    if (!read_meta_prefix_pair(meta_filename, index, count, prev_prefix, cur_prefix)) {
        std::cerr << "[Volume] 读 meta 失败或索引越界: index=" << index << std::endl;
        return false;
    }
    uint64_t start = prev_prefix;
    uint64_t size  = cur_prefix - prev_prefix;

    std::ifstream dfs(data_filename, std::ios::binary);
    if (!dfs) {
        std::cerr << "[Volume] 打开 data 失败: " << data_filename << std::endl;
        return false;
    }
    dfs.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    if (!dfs) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0) {
        dfs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
        if (!dfs) {
            std::cerr << "[Volume] 读取 data 失败 index=" << index << std::endl;
            return false;
        }
    }

    auto vol = Volume::deserialize(buf.data(), buf.size());
    if (!vol) {
        std::cerr << "[Volume] 反序列化失败 index=" << index << std::endl;
        return false;
    }

    // UUID 去重
    const std::string uid = vol->uuid();
    auto exists = std::any_of(out_list.begin(), out_list.end(),
                              [&](const std::shared_ptr<Volume>& v){ return v && v->uuid() == uid; });
    if (!exists) out_list.push_back(std::move(vol));
    return true;
}

bool FileSystem::load_nth_ssd_volume(uint32_t index,
                                     const std::string& meta_filename,
                                     const std::string& data_filename) {
    size_t before = ssd_volumes.size();
    bool ok = load_nth_volume_core(meta_filename, data_filename, index, ssd_volumes);
    if (ok && ssd_volumes.size() > before) {
        ssd_volume_indices.push_back(index);
    }
    return ok;
}

bool FileSystem::load_nth_hdd_volume(uint32_t index,
                                     const std::string& meta_filename,
                                     const std::string& data_filename) {
    size_t before = hdd_volumes.size();
    bool ok = load_nth_volume_core(meta_filename, data_filename, index, hdd_volumes);
    if (ok && hdd_volumes.size() > before) {
        hdd_volume_indices.push_back(index);
    }
    return ok;
}

int FileSystem::get_persisted_ssd_volume_count(const std::string& meta_filename) const {
    return get_volume_count_core(meta_filename);
}

int FileSystem::get_persisted_hdd_volume_count(const std::string& meta_filename) const {
    return get_volume_count_core(meta_filename);
}

// ============ 持久化第 n 个卷（meta+data） ============

bool FileSystem::persist_volume_at_index_core(const std::string& meta_filename,
                                               const std::string& data_filename,
                                               uint32_t index,
                                               const Volume& vol) {
    if (!ensure_meta_initialized(meta_filename)) {
        std::cerr << "[Volume] 初始化 meta 失败: " << meta_filename << std::endl;
        return false;
    }

    std::vector<uint8_t> blob = vol.serialize();
    const uint64_t new_size = blob.size();

    uint32_t count = 0;
    uint64_t last_prefix = 0;
    if (!read_meta_last_prefix(meta_filename, count, last_prefix)) {
        std::cerr << "[Volume] 读取 meta 尾部失败: " << meta_filename << std::endl;
        return false;
    }

    // 追加
    if (index == count) {
        // 写 data 尾部
        std::ofstream dfs(data_filename, std::ios::binary | std::ios::app);
        if (!dfs) {
            std::cerr << "[Volume] 打开 data 失败: " << data_filename << std::endl;
            return false;
        }
        if (new_size > 0) {
            dfs.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(new_size));
            if (!dfs) return false;
        }
        // 更新 meta：prefix 追加（last_prefix + new_size），count+1
        uint64_t new_prefix = last_prefix + new_size;
        if (!append_meta_prefix(meta_filename, new_prefix, count + 1)) {
            std::cerr << "[Volume] 追加 meta 失败" << std::endl;
            return false;
        }
        return true;
    }

    // 替换（index < count）：仅当同尺寸才可原地覆盖
    uint64_t prev_prefix = 0, cur_prefix = 0;
    if (!read_meta_prefix_pair(meta_filename, index, count, prev_prefix, cur_prefix)) {
        std::cerr << "[Volume] 读取 meta pair 失败" << std::endl;
        return false;
    }
    uint64_t old_size = cur_prefix - prev_prefix;
    if (old_size != new_size) {
        std::cerr << "[Volume] 替换尺寸不同，当前前缀和格式不支持原地重排（建议保持定长或改用偏移数组）"
                  << " index=" << index << " old=" << old_size << " new=" << new_size << std::endl;
        return false; // 若需要，可在外部走“重打包”路径
    }

    // 同尺寸替换：覆盖 data 指定偏移区间
    std::fstream dfs(data_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!dfs) {
        std::cerr << "[Volume] 打开 data 失败: " << data_filename << std::endl;
        return false;
    }
    dfs.seekp(static_cast<std::streamoff>(prev_prefix), std::ios::beg);
    if (!dfs) return false;
    if (new_size > 0) {
        dfs.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(new_size));
        dfs.flush();
        if (!dfs) return false;
    }
    // meta 无需改动
    return true;
}

bool FileSystem::persist_ssd_volume_at(uint32_t index, const std::shared_ptr<Volume>& vol,
                                       const std::string& meta_filename,
                                       const std::string& data_filename) {
    if (!vol) return false;
    return persist_volume_at_index_core(meta_filename, data_filename, index, *vol);
}

bool FileSystem::persist_hdd_volume_at(uint32_t index, const std::shared_ptr<Volume>& vol,
                                       const std::string& meta_filename,
                                       const std::string& data_filename) {
    if (!vol) return false;
    return persist_volume_at_index_core(meta_filename, data_filename, index, *vol);
}

bool FileSystem::append_ssd_volume(const std::shared_ptr<Volume>& vol,
                                   const std::string& meta_filename,
                                   const std::string& data_filename) {
    if (!vol) return false;
    int count = get_volume_count_core(meta_filename);
    uint32_t idx = (count < 0) ? 0u : static_cast<uint32_t>(count);
    return persist_volume_at_index_core(meta_filename, data_filename, idx, *vol);
}

bool FileSystem::append_hdd_volume(const std::shared_ptr<Volume>& vol,
                                   const std::string& meta_filename,
                                   const std::string& data_filename) {
    if (!vol) return false;
    int count = get_volume_count_core(meta_filename);
    uint32_t idx = (count < 0) ? 0u : static_cast<uint32_t>(count);
    return persist_volume_at_index_core(meta_filename, data_filename, idx, *vol);
}

bool FileSystem::register_volume2(const std::shared_ptr<Volume>& vol, VolumeType type, int* out_index, bool persist_now) {
    if (!vol) return false;

    std::vector<std::shared_ptr<Volume>>* vec = nullptr;
    std::vector<uint32_t>* idx_vec = nullptr;
    const char* meta_path = nullptr;
    const char* data_path = nullptr;
    uint32_t* next_index_ptr = nullptr;

    if (type == VolumeType::SSD) {
        vec = &ssd_volumes;
        idx_vec = &ssd_volume_indices;
        meta_path = SSD_VOLUME_META_PATH;
        data_path = SSD_VOLUME_DATA_PATH;
        next_index_ptr = &ssd_next_index;
    } else {
        vec = &hdd_volumes;
        idx_vec = &hdd_volume_indices;
        meta_path = HDD_VOLUME_META_PATH;
        data_path = HDD_VOLUME_DATA_PATH;
        next_index_ptr = &hdd_next_index;
    }

    const std::string uid = vol->uuid();
    auto it = std::find_if(vec->begin(), vec->end(),
                           [&](const std::shared_ptr<Volume>& v){ return v && v->uuid() == uid; });
    if (it != vec->end()) {
        size_t pos = static_cast<size_t>(std::distance(vec->begin(), it));
        if (pos < idx_vec->size()) {
            if (out_index) *out_index = static_cast<int>((*idx_vec)[pos]);
            return true;
        }
        // 已在内存但尚无索引：给唯一索引（不再从文件取0）
        uint32_t idx = *next_index_ptr;
        if (persist_now) {
            if (!ensure_meta_initialized(meta_path)) return false;
            // 立即落盘前，确保 idx 与 on-disk count 同步
            int cur_cnt = get_volume_count_core(meta_path);
            if (cur_cnt < 0) cur_cnt = 0;
            idx = static_cast<uint32_t>(cur_cnt);
            if (!persist_volume_at_index_core(meta_path, data_path, idx, *vol)) return false;
            *next_index_ptr = idx + 1;
        } else {
            (*next_index_ptr)++;
        }
        idx_vec->push_back(idx);
        if (out_index) *out_index = static_cast<int>(idx);
        return true;
    }

    // 新卷：分配索引（persist_now 决定来源）
    if (!ensure_meta_initialized(meta_path)) return false;

    uint32_t idx = 0;
    if (persist_now) {
        int cur_cnt = get_volume_count_core(meta_path);
        if (cur_cnt < 0) cur_cnt = 0;
        idx = static_cast<uint32_t>(cur_cnt);
        if (!persist_volume_at_index_core(meta_path, data_path, idx, *vol)) {
            std::cerr << "[register_volume2] 持久化卷失败 uuid=" << uid << " index=" << idx << std::endl;
            return false;
        }
        // 文件已增长，推进内存 next_index
        *next_index_ptr = std::max<uint32_t>(*next_index_ptr, idx + 1);
    } else {
        // 仅内存分配唯一索引
        idx = *next_index_ptr;
        (*next_index_ptr)++;
    }

    vec->push_back(vol);
    idx_vec->push_back(idx);
    if (out_index) *out_index = static_cast<int>(idx);
    return true;
}

bool FileSystem::register_volume( // old 需修改
    const std::string& uuid,
    const std::string& storage_node_id,
    size_t total_blocks,
    VolumeType type,
    size_t block_size,
    size_t blocks_per_group
) {
    auto vol = std::make_shared<Volume>(uuid, storage_node_id, total_blocks, block_size, blocks_per_group);
    if (type == VolumeType::SSD) {
        ssd_volumes.push_back(vol);
    } else if (type == VolumeType::HDD) {
        hdd_volumes.push_back(vol);
    } else {
        return false;
    }
    // 假设已有创建好的 std::shared_ptr<Volume> new_vol;
    // 将“创建后加入内存”的旧逻辑替换为调用指针重载统一落盘与记录索引：
    // return register_volume(new_vol, type /*, 可选 out_index 指针*/);
    return true;
}

bool FileSystem::register_volume(const std::shared_ptr<Volume>& vol, VolumeType type) {
    if (!vol) return false;
    if (type == VolumeType::SSD) {
        ssd_volumes.push_back(vol);
    } else if (type == VolumeType::HDD) {
        hdd_volumes.push_back(vol);
    } else {
        return false;
    }
    return true;
}

void FileSystem::alloc_volume_for_inode(const std::shared_ptr<Inode>& inode) {
    const size_t reserve_blocks = 128;

    // 目录强制分配到0号SSD卷
    if (inode->file_mode.fields.file_type == static_cast<uint8_t>(FileType::Directory)) {
        if (ssd_volumes.size() <= 1) {
            std::cerr << "[ALLOC_VOLUME] SSD卷数量不足，无法分配目录" << std::endl;
            inode->volume_id.clear();
            return;
        }
        // auto& vol = ssd_volumes[1];  // HAHA
        auto& vol = ssd_volumes[0];           
        size_t free_blocks = vol->block_manager().free_blocks_count();
        if (free_blocks > reserve_blocks) {
            inode->volume_id = vol->uuid();
            return;
        } else {
            std::cerr << "[ALLOC_VOLUME] 1号SSD卷空间不足，无法分配目录" << std::endl;
            inode->volume_id.clear();
            return;
        }
    }

    // 普通文件顺序分配
    // static size_t last_ssd_idx = 0;
    size_t last_ssd_idx = 0;
    size_t n = ssd_volumes.size();
    // std::cout << n << std::endl;
    for (size_t i = 1; i < n; ++i) {
        // if(i == 1) continue; // 跳过1号SSD卷
        size_t idx = (last_ssd_idx + i) % n;
        auto& vol = ssd_volumes[idx];
        size_t free_blocks = vol->block_manager().free_blocks_count();
        if (free_blocks > reserve_blocks) {
            inode->volume_id = vol->uuid();
            last_ssd_idx = idx;
            return;
        }
    }

    // HDD卷分配
    n = hdd_volumes.size();
    static size_t last_hdd_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (last_hdd_idx + i) % n;
        auto& vol = hdd_volumes[idx];
        size_t free_blocks = vol->block_manager().free_blocks_count();
        if (free_blocks > reserve_blocks) {
            inode->volume_id = vol->uuid();
            last_hdd_idx = idx;
            return;
        }
    }

    inode->volume_id.clear();
    std::cerr << "[ALLOC_VOLUME] 所有卷空间不足，无法分配" << std::endl;
}

std::shared_ptr<Volume> FileSystem::find_volume_by_inode(const std::shared_ptr<Inode>& inode) {
    if (inode->volume_id.empty()) return nullptr;
    // 在SSD卷中查找
    for (const auto& vol : ssd_volumes) {
        if (vol->uuid() == inode->volume_id) return vol;
    }
    // 在HDD卷中查找
    for (const auto& vol : hdd_volumes) {
        if (vol->uuid() == inode->volume_id) return vol;
    }
    return nullptr;
}

bool FileSystem::create_file(const std::string& path, mode_t mode) {
    // 检查文件是否已存在
    if((get_inode_number(path)) != -1) {
        std::cerr << "[CREATE ERROR] 文件已存在: " << path << std::endl;
        return false; // 文件已存在
    }

    // 检查路径是否合法并提取父目录名和文件名
    if (path.empty() || path[0] != '/') {
        std::cerr << "[CREATE ERROR] 路径必须是绝对路径" << std::endl;
        return false; // 必须是绝对路径
    }

    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == path.length() - 1) {
        std::cerr << "[CREATE ERROR] 无效路径格式" << std::endl;
        return false; // 无效路径格式
    }

    std::string filename = path.substr(last_slash + 1);
    std::string parent_path = (last_slash == 0) ? "/" : path.substr(0, last_slash);

    // 检查父目录是否存在
    auto parent_ino = get_inode_number(parent_path);
    if (parent_ino == -1) {
        std::cerr << "[CREATE ERROR] 父目录不存在: " << parent_path << std::endl;
        return false; // 父目录不存在
    }
    auto parent_inode = std::make_shared<Inode>();
    metadata_manager->get_inode_storage()->read_inode(parent_ino,*parent_inode);

    // 创建新文件的inode
    auto new_inode = std::make_shared<Inode>();
    new_inode->setFileType(static_cast<uint8_t>(FileType::Regular));
    new_inode->setFilePerm(mode & 0xFFF);
    new_inode->setFmTime(InodeTimestamp());
    new_inode->setFaTime(InodeTimestamp());
    new_inode->setFcTime(InodeTimestamp());
    new_inode->setFilename(path);
    // new_inode->inode = allocate_inode(mode);
    new_inode->inode = metadata_manager->allocate_inode(mode);
    if (new_inode->inode == static_cast<uint64_t>(-1)) {
        std::cerr << "[CREATE ERROR] 无法分配inode号" << std::endl;
        return false; // 分配inode失败
    }
    LOGD("[CREATE] 分配的inode号: " << new_inode->inode);
    // std::cout << "[CREATE] 分配的inode号: " << new_inode->inode << std::endl;

    alloc_volume_for_inode(new_inode);
    if (new_inode->volume_id.empty()) {
        std::cerr << "[CREATE ERROR] 无法为新文件分配卷" << std::endl;
        return false;
    }

    // 更新父目录项
    DirectoryEntry file_entry(filename, new_inode->inode, FileType::Regular);
    if (!addDirectoryEntry(parent_inode, file_entry) ) {
        // 回滚：释放已分配的资源
        // 需要根据你的实现添加相应的清理代码
        return false;
    }


    if(metadata_manager->get_inode_storage()->write_inode(new_inode->inode, *new_inode)){
        // LOGD("[CREATE] 写入inode成功: " << new_inode->inode);
        LOGI("[CREATE SUCCESS] 文件创建成功: " << path);
    } else {
        std::cerr << "[CREATE ERROR] 无法写入inode到存储" << std::endl;
        // LOGE("[CREATE ERROR] 无法写入inode到存储");
        inode_table.erase(path); // 清理已注册的inode
        return false;
    }

    inode_table[path] = new_inode->inode;

    return true;
}

bool FileSystem::remove_file(const std::string& path) {
    // 检查文件是否存在
    auto inode_no = get_inode_number(path);
    if (inode_no == -1) {
        std::cerr << "[REMOVE ERROR] 文件不存在: " << path << std::endl;
        // LOGE("[REMOVE ERROR] 文件不存在: " << path);
        return false; // 文件不存在
    }
    auto inode = std::make_shared<Inode>();
    if (!metadata_manager->get_inode_storage()->read_inode(inode_no, *inode)) {
        return false;
    }

    // 提取父目录名和文件名
    // 不能删除根目录
    if (path.empty() || path[0] != '/') {
        return false; // 必须是绝对路径
    }
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == path.length() - 1) {
        return false; // 无效路径格式
    }
    std::string filename = path.substr(last_slash + 1);
    std::string parent_path = (last_slash == 0) ? "/" : path.substr(0, last_slash);

    // 检查父目录是否存在
    auto parent_ino = get_inode_number(parent_path);
    if (parent_ino == -1) {
        return false; // 父目录不存在
    }
    auto parent_inode = std::make_shared<Inode>();
    metadata_manager->get_inode_storage()->read_inode(parent_ino,*parent_inode);

    auto volume = find_volume_by_inode(inode);
    if (!volume) {
        std::cerr << "[REMOVE ERROR] 未找到对应卷" << std::endl;
        return false;
    }

    // 更新父目录目录项
    if(!removeDirectoryEntry(parent_inode, filename)){
        std::cerr << "[REMOVE ERROR] 父目录没有该文件目录项" << std::endl;
        return false;
    }

    // 释放存储块
    for (const auto& seg : inode->block_segments) {
        volume->free_blocks(seg);
    }
    inode->block_segments.clear();

    // 更新文件句柄表，关闭所有相关文件描述符
    std::vector<int> fds_to_close;
    for (const auto& [fd, entry] : fd_table) {
        if (entry.inode && entry.inode->inode == inode_no) fds_to_close.push_back(fd);
    }
    for (int fd : fds_to_close) close(fd);

    // 更新 inode_table
    auto it = inode_table.find(path);
    if (it != inode_table.end()) {
        inode_table.erase(it);
    }

    // TODO 释放 inode 号
    // metadata_manager->free_inode(inode_no);

    return true;
}

int FileSystem::get_free_fd() {
    size_t fd = fd_bitmap.find_first(); // 找到第一个为1的位置
    if (fd == boost::dynamic_bitset<>::npos) {
        // 没有空闲fd，扩容到2倍
        LOGD("[FD ERROR] 没有空闲文件描述符，扩容...");
        // std::cout << "[FD ERROR] 没有空闲文件描述符，扩容..." << std::endl;
        size_t new_size = std::max<size_t>(fd_bitmap.size() * 2, 1);
        fd = fd_bitmap.size();
        fd_bitmap.resize(new_size, true);
    }
    fd_bitmap.reset(fd); // 标记为已分配
    return static_cast<int>(fd);
}

int FileSystem::open(const std::string& path, int flags, mode_t mode) {
    auto it = inode_table.find(path);
    if (it == inode_table.end()) {
        // LOGE("[OPEN ERROR] 文件不存在: " << path);
        std::cerr << "[OPEN ERROR] 文件不存在: " << path << std::endl;
        return -1; // 文件不存在
    }
    uint64_t inode_no = it->second;
    auto inode = std::make_shared<Inode>();
    if (!metadata_manager->get_inode_storage()->read_inode(inode_no, *inode)) {
        // LOGE("[OPEN ERROR] 无法读取inode: " << inode_no);
        std::cerr << "[OPEN ERROR] 无法读取inode: " << inode_no << std::endl;
        return -1;
    }
    int fd = get_free_fd();
    LOGD("[OPEN] 分配的文件描述符: " << fd);
    // std::cout << "[OPEN] 分配的文件描述符: " << fd << std::endl;
    FdTableEntry entry;
    entry.inode = inode;
    entry.offset = 0;
    entry.flags = flags;
    fd_table[fd] = entry;
    inode->setFaTime(InodeTimestamp());
    metadata_manager->get_inode_storage()->write_inode(inode->inode, *inode);
    return fd;
}

// 写入文件
ssize_t FileSystem::write(int fd, const char* buf, size_t count) {
    // 查找文件描述符
    auto it = fd_table.find(fd);
    if (it == fd_table.end()) {
        return -1; // 无效fd
    }
    
    FdTableEntry& entry = it->second; // 获取完整的描述符表项

    if(entry.flags & MO_RDONLY) {
        std::cerr << "[WRITE ERROR] 文件以只读模式打开" << std::endl;
        return -1; // 只读模式不能写入
    }

    auto inode = entry.inode;        // 获取对应的inode
    size_t& offset = entry.offset;   // 引用当前偏移量
    // 计算需要的总块数
    size_t total_needed_size = offset + count;
    size_t first_block = offset / BLOCK_SIZE;
    size_t last_block = (offset + count - 1) / BLOCK_SIZE;
    size_t total_blocks_needed = last_block + 1;
    
    // 获取文件当前分配的块数量
    // size_t existing_blocks = 0;
    // for (const auto& seg : inode->block_segments) existing_blocks += seg.block_count;

    // 1. 统计已分配的逻辑块
    std::vector<bool> logical_blocks(total_blocks_needed, false);
    for (const auto& seg : inode->block_segments) {
        for (size_t i = 0; i < seg.block_count; ++i) {
            size_t lbn = seg.logical_start + i;
            if (lbn < logical_blocks.size()) logical_blocks[lbn] = true;
        }
    }

    // 2. 统计需要新分配的块
    size_t new_blocks_needed = 0;
    for (size_t i = 0; i < logical_blocks.size(); ++i)
        if (!logical_blocks[i]) ++new_blocks_needed;

    // 通过inode查找卷
    auto volume = find_volume_by_inode(inode);
    if (!volume) {
        std::cerr << "[WRITE ERROR] 未找到对应卷" << std::endl;
        return -1;
    }

    // 输出当前文件的所有块
    LOGD("[WRITE] All blocks have allocated to file: [");
    for (size_t i = 0; i < inode->block_segments.size(); i++) {
        if (i > 0) LOGD(", ");
        LOGD(inode->block_segments[i].start_block << " ("
                  << inode->block_segments[i].block_count << " blocks)");
    }
    LOGD("]");

    if (new_blocks_needed > 0) {
        // 输出提示信息，表示需要分配新块
        LOGD("[WRITE] 需要分配新块: " << new_blocks_needed << " blocks");
        // std::cout << "[WRITE] 分配新块: " << new_blocks_needed << " blocks" << std::endl;

        auto new_segments = volume->allocate_blocks(AllocType::DATA, new_blocks_needed);
        // 将新分配的块映射到未分配的逻辑块
        size_t seg_idx = 0, blk_in_seg = 0;
        for (size_t lbn = 0; lbn < logical_blocks.size(); ++lbn) {
            if (logical_blocks[lbn]) continue;
            if (blk_in_seg >= new_segments[seg_idx].block_count) {
                ++seg_idx; blk_in_seg = 0;
            }
            // 插入新的 BlockSegment（每个新块单独一个段，后续可合并）
            inode->block_segments.emplace_back(lbn, new_segments[seg_idx].start_block + blk_in_seg, 1);
            ++blk_in_seg;
        }
        // 合并相邻段
        std::sort(inode->block_segments.begin(), inode->block_segments.end(),
                  [](const BlockSegment& a, const BlockSegment& b) { return a.logical_start < b.logical_start; });
        std::vector<BlockSegment> merged;
        for (const auto& seg : inode->block_segments) {
            if (!merged.empty() &&
                merged.back().logical_start + merged.back().block_count == seg.logical_start &&
                merged.back().start_block + merged.back().block_count == seg.start_block) {
                merged.back().block_count += seg.block_count;
            } else {
                merged.push_back(seg);
            }
        }
        inode->block_segments = std::move(merged);
    }
    //输出提示信息，表示分配新块完成
    LOGD("[WRITE] 分配新块完成，当前文件的所有块: [");
    for (size_t i = 0; i < inode->block_segments.size(); i++) {
        if (i > 0) LOGD(", ");
        LOGD(inode->block_segments[i].start_block << " ("
                  << inode->block_segments[i].block_count << " blocks)");
    }
    LOGD("]");

    // 4. 收集所有IO请求，然后合并下发
    std::vector<IORequest> io_requests;

    size_t bytes_written = 0;
    size_t remain = count;
    size_t buf_offset = 0;
    size_t file_offset = offset;

    while (remain > 0) {
        size_t logical_block = file_offset / BLOCK_SIZE;
        size_t in_block_offset = file_offset % BLOCK_SIZE;
        size_t physical_block = 0;
        if (!inode->find_physical_block(logical_block, physical_block)) break;
        size_t write_len = std::min(BLOCK_SIZE - in_block_offset, remain);

        // 直接在IORequest中包含缓冲区指针和大小
        IORequest req(IOType::Write, volume->storage_node_id(), volume->uuid(), 
                    physical_block, 1, in_block_offset, write_len,
                    const_cast<char*>(buf + buf_offset), write_len);
        io_requests.push_back(req);

        buf_offset += write_len;
        remain -= write_len;
        file_offset += write_len;
        bytes_written += write_len;
    }

    // 批量下发IO请求（内部会自动合并相邻请求）
    if (!io_requests.empty()) {
        volume->submit_io_requests(io_requests);
    }

    // 更新元数据
    // if(total_needed_size > inode->file_size.fields.file_size) {
    //     inode->file_size.fields.file_size = total_needed_size;
    // }
    inode->file_size.fields.file_size = total_needed_size;
    // std::cout << "[WRITE] 文件大小更新为: " << inode->file_size.fields.file_size << " bytes" << std::endl;
    inode->setFmTime(InodeTimestamp());
    offset = total_needed_size; // 更新文件指针
    if(metadata_manager->get_inode_storage()->write_inode(inode->inode, *inode)){
        LOGD("[WRITE] 写入inode成功: " << inode->inode);
        // std::cout << "[Write SUCCESS] 文件写入后，元数据更新成功: (inode_no)" << inode->inode << std::endl;
    }
    
    return bytes_written; // 返回成功写入的字节数
}

// 读取文件
ssize_t FileSystem::read(int fd, char* buf, size_t count) {
    // 查找文件描述符
    auto it = fd_table.find(fd);
    if (it == fd_table.end()) {
        // LOGE("[READ ERROR] 无效的文件描述符: " << fd);
        std::cerr << "[READ ERROR] 无效的文件描述符: " << fd << std::endl;
        return -1; // 无效fd
    }
    
    FdTableEntry& entry = it->second;

    if(entry.flags & MO_WRONLY) {
        std::cerr << "[READ ERROR] 文件以只写模式打开" << std::endl;
        return -1; // 只写模式不能读取
    }

    auto inode = entry.inode;
    size_t offset = 0; // 由于暂未实现句柄的读写控制，仅作测试功能使用
    // size_t& offset = entry.offset;
    
    // 检查是否还有数据可读
    if (offset >= inode->file_size.fields.file_size) {
        LOGD("[READ] 当前偏移量: " << offset << ", 文件大小: " 
                  << inode->file_size.fields.file_size);
        // std::cout << "[READ] 当前偏移量: " << offset << ", 文件大小: " 
        //           << inode->file_size.fields.file_size << std::endl;
        LOGD("[READ] 已到文件末尾，无法读取更多数据");
        // std::cout << "[READ] 已到文件末尾，无法读取更多数据" << std::endl;
        return 0; // 已到文件末尾
    }
    
    // 计算实际可读取字节数
    size_t read_len = std::min(count, inode->file_size.fields.file_size - offset);

    // 通过inode查找卷
    auto volume = find_volume_by_inode(inode);
    if (!volume) {
        std::cerr << "[READ ERROR] 未找到对应卷" << std::endl;
        return -1;
    }
    LOGD("[READ] 读取 " << read_len << " 字节 from volume " << volume->uuid());

    // 收集所有IO请求，然后合并下发
    std::vector<IORequest> io_requests;

    size_t current_offset = offset;
    size_t end_offset = offset + read_len;
    size_t buf_offset = 0;

    while (current_offset < end_offset) {
        size_t logical_block = current_offset / BLOCK_SIZE;
        size_t block_offset = current_offset % BLOCK_SIZE;
        size_t physical_block = 0;
        
        if (!inode->find_physical_block(logical_block, physical_block)) {
            // 稀疏区间，填0
            size_t bytes_in_this_block = std::min(BLOCK_SIZE - block_offset, end_offset - current_offset);
            memset(buf + buf_offset, 0, bytes_in_this_block);
            buf_offset += bytes_in_this_block;
            current_offset += bytes_in_this_block;
            LOGD("[READ] 稀疏区间填0: offset " << current_offset - bytes_in_this_block
                    << ", 长度: " << bytes_in_this_block);
            // std::cout << "[READ] 稀疏区间填0: offset " << current_offset - bytes_in_this_block
            //         << ", 长度: " << bytes_in_this_block << std::endl;
            continue;
        }
        
        size_t bytes_in_this_block = std::min(BLOCK_SIZE - block_offset, end_offset - current_offset);
        
        // 直接在IORequest中包含缓冲区指针和大小
        IORequest req(IOType::Read, volume->storage_node_id(), volume->uuid(), 
                    physical_block, 1, block_offset, bytes_in_this_block,
                    buf + buf_offset, bytes_in_this_block);
        io_requests.push_back(req);
        
        LOGD("[READ] 生成IO请求: 逻辑块 " << logical_block 
                << " -> 物理块 " << physical_block 
                << ", 偏移: " << block_offset 
                << ", 长度: " << bytes_in_this_block);
        
        buf_offset += bytes_in_this_block;
        current_offset += bytes_in_this_block;
    }


    // 批量下发IO请求（内部会自动合并相邻请求）
    if (!io_requests.empty()) {
        volume->submit_io_requests(io_requests);
    }

    // 输出buffer内容
    LOGD("[READ] 读取内容: ");
    // std::cout << "[READ] 读取内容: ";
    for (size_t i = 0; i < read_len; ++i) {
        LOGD(static_cast<int>(buf[i]) << " ");
        // std::cout << static_cast<int>(buf[i]) << " ";
    }
    LOGD(std::dec);
    
    // 更新偏移量
    offset += read_len;
    
    // 更新访问时间
    inode->setFaTime(InodeTimestamp());
    metadata_manager->get_inode_storage()->write_inode(inode->inode, *inode);
    std::cout << "[READ] 成功读取 " << read_len << " 字节" << std::endl;
    
    return read_len;
}

// 关闭文件
void FileSystem::close(int fd) {
    fd_table.erase(fd); 
    if (fd >= 0 && static_cast<size_t>(fd) < fd_bitmap.size()) {
        fd_bitmap.set(fd); // 标记为未分配
    }
}

// @wbl
/**
 * 向目录中添加一个新目录项
 * @param dir_inode 目录的inode
 * @param new_entry 要添加的新目录项
 * @return 是否添加成功
 */
bool FileSystem::addDirectoryEntry(const std::shared_ptr<Inode>& dir_inode, const DirectoryEntry& new_entry) {
    auto volume = find_volume_by_inode(dir_inode);
    if (volume == nullptr) {
        std::cerr << "[ADD DIRECTORY ENTRY ERROR] 未找到对应卷" << std::endl;
        return false;
    }
    size_t BLOCK_SIZE_T = volume->block_manager().block_size();

    // 1. 读取目录现有的所有块
    auto& segments = dir_inode->block_segments;
    std::vector<uint8_t> block_data(BLOCK_SIZE_T, 0);
    
    // 2. 遍历所有块，寻找可以插入新目录项的空间
    for (auto& seg : segments) {
        for (size_t block_offset = 0; block_offset < seg.block_count; ++block_offset) {
            size_t block_num = seg.start_block + block_offset;
            volume->read_block(block_num, block_data.data(), BLOCK_SIZE_T);
            
            size_t pos = 0;
            DirectoryEntry* prev_entry = nullptr;
            
            while (pos < BLOCK_SIZE_T) {
                auto entry = reinterpret_cast<DirectoryEntry*>(block_data.data() + pos);
                
                // 检查是否是空闲目录项(inode == 0)
                if (entry->inode == 0) {
                    // 整个空闲空间可以尝试分配
                    if (entry->rec_len >= new_entry.rec_len) {
                        // 可以在此处插入新目录项
                        memcpy(entry, &new_entry, new_entry.rec_len);
                        
                        // 如果还有剩余空间，创建一个新的空闲目录项
                        if (entry->rec_len > new_entry.rec_len) {
                            auto free_entry = reinterpret_cast<DirectoryEntry*>(
                                block_data.data() + pos + new_entry.rec_len);
                            free_entry->inode = 0;
                            free_entry->rec_len = entry->rec_len - new_entry.rec_len;
                        }
                        
                        // 写回块
                        volume->write_block(block_num, block_data.data(), BLOCK_SIZE_T);
                        return true;
                    }
                } else {
                    // 有效目录项，计算实际使用空间
                    size_t actual_entry_size = offsetof(DirectoryEntry, name) + entry->name_len;
                    actual_entry_size = (actual_entry_size + 7) & ~0x7; // 8字节对齐
                    
                    // 检查是否有足够空间插入新目录项
                    if (entry->rec_len >= actual_entry_size + new_entry.rec_len) {
                        // 可以在此处插入新目录项
                        
                        // 在剩余空间中插入新目录项
                        DirectoryEntry* new_entry_ptr = reinterpret_cast<DirectoryEntry*>(
                            block_data.data() + pos + actual_entry_size);
                        memcpy(new_entry_ptr, &new_entry, new_entry.rec_len);
                        
                        // 新目录项的rec_len覆盖剩余所有空间
                        new_entry_ptr->rec_len = entry->rec_len - actual_entry_size;

                        // 调整原来目录项的rec_len为实际使用大小
                        entry->rec_len = actual_entry_size;
                        
                        // 写回块
                        volume->write_block(block_num, block_data.data(), BLOCK_SIZE_T);
                        return true;
                    }
                }
                
                // 移动到下一个目录项
                pos += entry->rec_len;
            }
        }
    }
    
    // 3. 所有块都没有足够空间，分配新块
    auto new_seg = volume->allocate_blocks(AllocType::DATA, 1);
    if (new_seg.empty()) {
        return false;
    }
    
    dir_inode->block_segments.push_back(new_seg[0]);
    
    // 在新块中添加目录项
    std::vector<uint8_t> new_block(BLOCK_SIZE_T, 0);
    DirectoryEntry* new_entry_ptr = reinterpret_cast<DirectoryEntry*>(new_block.data());
    memcpy(new_entry_ptr, &new_entry, new_entry.rec_len);
    
    // 新块的第一个目录项的rec_len覆盖整个块
    new_entry_ptr->rec_len = BLOCK_SIZE_T;
    
    volume->write_block(new_seg[0].start_block, new_block.data(), BLOCK_SIZE_T);
    // 更新目录元数据
    dir_inode->file_size.fields.file_size += new_entry.rec_len;
    dir_inode->setFmTime(InodeTimestamp());
    if(!metadata_manager->get_inode_storage()->write_inode(dir_inode->inode, *dir_inode)){
        std::cerr << "[ADD DIRECTORY ENTRY ERROR] 元数据更新失败: (inode_no)" << dir_inode->inode << std::endl;
        return false;
    }
    return true;
}

// @wbl
/**
 * 从目录中删除指定名称的目录项
 * @param dir_inode 目录的inode
 * @param name 要删除的目录项名称
 * @return 是否删除成功
 */
bool FileSystem::removeDirectoryEntry(const std::shared_ptr<Inode>& dir_inode, const std::string& name) {
    auto volume = find_volume_by_inode(dir_inode);
    if (volume == nullptr) {
        std::cerr << "[REMOVE DIRECTORY ENTRY ERROR] 未找到对应卷" << std::endl;
        return false;
    }
    size_t BLOCK_SIZE_T = volume->block_manager().block_size();

    auto& segments = dir_inode->block_segments;
    std::vector<uint8_t> block_data(BLOCK_SIZE_T, 0);

    // 遍历所有块查找要删除的目录项
    for (auto& seg : segments) {
        for (size_t block_offset = 0; block_offset < seg.block_count; ++block_offset) {
            size_t block_num = seg.start_block + block_offset;
            volume->read_block(block_num, block_data.data(), BLOCK_SIZE_T);
            
            size_t pos = 0;
            DirectoryEntry* prev_entry = nullptr;
            
            while (pos < BLOCK_SIZE_T) {
                auto entry = reinterpret_cast<DirectoryEntry*>(block_data.data() + pos);
                
                // 检查是否是目标目录项
                if (entry->inode != 0 && std::string(entry->name, entry->name_len) == name) {
                    // 找到要删除的目录项
                    if (prev_entry) {
                        // 前一个目录项存在，将其rec_len扩展到覆盖当前目录项
                        prev_entry->rec_len += entry->rec_len;
                    } else {
                        // 这是块中的第一个目录项，标记为删除
                        entry->inode = 0;
                    }
                    
                    // 写回块
                    volume->write_block(block_num, block_data.data(), BLOCK_SIZE_T);
                    return true;
                }
                
                // 更新前一个目录项指针
                prev_entry = entry;
                pos += entry->rec_len;
            }
        }
    }
    
    return false; // 没有找到要删除的目录项
}

// @wbl
/**
 * 读取目录中的所有目录项
 * @param dir_inode 目录的inode
 * @return 包含所有有效目录项的vector
 */
std::vector<DirectoryEntry> FileSystem::readDirectoryEntries(const std::shared_ptr<Inode>& dir_inode) {
    std::vector<DirectoryEntry> entries;
    auto volume = find_volume_by_inode(dir_inode);
    if (volume == nullptr) {
        std::cerr << "[READ DIRECTORY ENTRIES ERROR] 未找到对应卷" << std::endl;
        return entries;
    }
    size_t BLOCK_SIZE_T = volume->block_manager().block_size();

    auto& segments = dir_inode->block_segments;
    std::vector<uint8_t> block_data(BLOCK_SIZE_T, 0);
    
    for (const auto& seg : segments) {
        for (size_t block_offset = 0; block_offset < seg.block_count; ++block_offset) {
            size_t block_num = seg.start_block + block_offset;
            volume->read_block(block_num, block_data.data(), BLOCK_SIZE_T);
            
            size_t pos = 0;
            while (pos < BLOCK_SIZE_T) {
                auto entry = reinterpret_cast<DirectoryEntry*>(block_data.data() + pos);
                
                // 只收集有效目录项
                if (entry->inode != 0) {
                    DirectoryEntry valid_entry(
                        std::string(entry->name, entry->name_len),
                        entry->inode,
                        entry->file_type);
                    valid_entry.rec_len = entry->rec_len;
                    
                    entries.push_back(valid_entry);
                }
                
                pos += entry->rec_len;
            }
        }
    }
    
    return entries;
}

bool FileSystem::ls(const std::string& path){
    // 1. 查找路径对应的inode
    auto ino = get_inode_number(path);
    if (ino == -1) {
        std::cerr << "[LS ERROR] 无效路径或inode不存在: " << path << std::endl;
        return false; // 无效路径或inode不存在
    }

    auto inode = std::make_shared<Inode>();
    if (!metadata_manager->get_inode_storage()->read_inode(ino, *inode)) {
        std::cerr << "[LS ERROR] 无法读取inode: " << ino << std::endl;
        return false; // 无法读取inode
    }

    // 2. 检查是否为目录
    if (inode->file_mode.fields.file_type != static_cast<uint8_t>(FileType::Directory)) {
        std::cerr << "[LS ERROR] 不是目录: " << path << std::endl;
        return false;
    }

    // 3. 读取目录项
    auto entries = readDirectoryEntries(inode);
    
    // 4. 输出目录项信息
    std::cout << "[LS] 目录: " << path << " (inode: " << ino << ")" << std::endl;
    if (entries.empty()) {
        std::cout << "目录为空" << std::endl;
        return true; // 目录为空
    }

    for (auto& entry : entries){ 
        entry.name[entry.name_len] = '\0'; // 确保字符串以null结尾
        std::cout << entry.name << " (inode: " << entry.inode 
                  << ", type: " << static_cast<int>(entry.file_type) 
                  << ")" << std::endl;
    }
    
    return true;
}

// @wbl
// 路径解析：根据绝对路径，返回 inode 指针
std::shared_ptr<Inode> FileSystem::find_inode_by_path(const std::string& path) {
    // 检查路径是否合法
    if (path.empty() || path[0] != '/') {
        return nullptr; // 路径不合法
    }

    if(path == "/"){
        const uint64_t root_inode_number = get_root_inode();       // TODO: 根目录 inode 号暂定为 2 
        std::shared_ptr<Inode> root_inode = std::make_shared<Inode>();
        metadata_manager->get_inode_storage()->read_inode(root_inode_number, *root_inode);

        return root_inode;
    }

    // 获取路径中的目录名和文件名
    size_t last_slash = path.find_last_of('/');
    std::string dirname = (last_slash == 0) ? "/" : path.substr(0, last_slash);
    std::string filename = path.substr(last_slash + 1);

    // 递归查找目录 inode
    std::shared_ptr<Inode> dir_inode = find_inode_by_path(dirname);
    if (!dir_inode) {
        return nullptr; // 父目录不存在
    }

    // 读取目录项
    auto entries = readDirectoryEntries(dir_inode);
    for (const auto& entry : entries) {
        if (std::string(entry.name, entry.name_len) == filename) {
            // 找到目标文件,返回其 inode
            std::shared_ptr<Inode> file_inode = std::make_shared<Inode>();
            if (metadata_manager->get_inode_storage()->read_inode(entry.inode, *file_inode)) {
                return file_inode;
            }
        }
    }

    return nullptr; // 未找到目标文件
}

// @wbl
bool FileSystem::mkdir(const std::string& path, mode_t mode) {
    // 1. 检查路径是否合法并提取目录名和父目录路径
    if (path.empty() || path[0] != '/') {
        return false; // 必须是绝对路径
    }

    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == path.length() - 1) {
        return false; // 无效路径格式
    }

    std::string dirname = path.substr(last_slash + 1);
    std::string parent_path = (last_slash == 0) ? "/" : path.substr(0, last_slash);

    // 2. 检查父目录是否存在
    auto parent_ino = get_inode_number(parent_path);
    if (parent_ino == -1) {
        return false; // 父目录不存在
    }
    auto parent_inode = std::make_shared<Inode>();
    metadata_manager->get_inode_storage()->read_inode(parent_ino,*parent_inode);

    // 3. 检查目录是否已存在
    if (inode_table.find(path) != inode_table.end()) {
        return false; // 目录已存在
    }

    // 4. 创建目录 inode
    auto dir_inode = std::make_shared<Inode>();
    dir_inode->setFilename(dirname);  // 只存储目录名，不是完整路径
    dir_inode->setFileType(static_cast<uint8_t>(FileType::Directory));
    dir_inode->setFilePerm(mode & 0777);
    dir_inode->setFileSize(0);
    
    // 设置时间戳
    InodeTimestamp now;
    dir_inode->setFmTime(now);
    dir_inode->setFaTime(now);
    dir_inode->setFcTime(now);

    // 5. 分配inode号（TODO 需要适配 MetadataManager 实现）
    uint64_t new_inode = metadata_manager->allocate_inode(mode);
    if (new_inode == 0) {
        return false; // 分配inode失败
    }
    dir_inode->inode = new_inode;

    try {
        // 6. 分配存储空间
        alloc_volume_for_inode(dir_inode);
        
        // 7. 构造初始目录内容
        DirectoryEntry self_entry(".", dir_inode->inode, FileType::Directory);
        DirectoryEntry parent_entry("..", parent_ino, FileType::Directory);

        // 8. 写入目录项
        if (!addDirectoryEntry(dir_inode, self_entry) || 
            !addDirectoryEntry(dir_inode, parent_entry)) {
            // 回滚：释放已分配的资源
            // 需要根据你的实现添加相应的清理代码
            return false;
        }

    } catch (...) {
        // 异常处理：释放已分配的资源
        return false;
    }

    // 9. 更新inode表和父目录的修改时间
    inode_table[path] = new_inode;

    parent_inode->setFmTime(now); // 更新父目录修改时间

    // 更新父目录的目录项
    DirectoryEntry new_dir_entry(dirname, new_inode, FileType::Directory);
    if (!addDirectoryEntry(parent_inode, new_dir_entry)) {
        inode_table.erase(path); // 清理已创建的目录
        return false; // 添加目录项失败
    }

    // 持久化 inode
    if (!metadata_manager->get_inode_storage()->write_inode(new_inode, *dir_inode)) {
        inode_table.erase(path); // 清理已创建的目录
        return false; // 持久化失败
    }

    // 持久化父目录 inode
    if (!metadata_manager->get_inode_storage()->write_inode(parent_ino, *parent_inode)) {
        inode_table.erase(path); // 清理已创建的目录
        return false; // 持久化失败
    }

    return true;
}

// @wbl
bool FileSystem::rmdir(const std::string& path) {
    // 检查文件是否存在
    auto inode_no = get_inode_number(path);
    if (inode_no == -1) {
        std::cerr << "[REMOVE ERROR] 文件不存在: " << path << std::endl;
        return false; // 文件不存在
    }
    auto inode = std::make_shared<Inode>();
    if (!metadata_manager->get_inode_storage()->read_inode(inode_no, *inode)) {
        return false;
    }

    // 提取父目录名和文件名
    // 不能删除根目录
    if (path.empty() || path[0] != '/') {
        return false; // 必须是绝对路径
    }
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == path.length() - 1) {
        return false; // 无效路径格式
    }
    std::string dirname = path.substr(last_slash + 1);
    std::string parent_path = (last_slash == 0) ? "/" : path.substr(0, last_slash);

    // 检查父目录是否存在
    auto parent_ino = get_inode_number(parent_path);
    if (parent_ino == -1) {
        return false; // 父目录不存在
    }
    auto parent_inode = std::make_shared<Inode>();
    metadata_manager->get_inode_storage()->read_inode(parent_ino,*parent_inode);
    
    // 检查目录是否为空（只应包含.和..）
    auto entries = readDirectoryEntries(inode);
    if (entries.size() > 2) {
        std::cerr << "[REMOVE ERROR] 目录非空" << std::endl;
        return false; // 目录非空
    }

    // 释放目录占用的块
    auto volume = find_volume_by_inode(inode);
    if (!volume) {
        std::cerr << "[REMOVE ERROR] 未找到对应卷" << std::endl;
        return false;
    }
    for (const auto& seg : inode->block_segments) {
        volume->free_blocks(seg);
    }

    // 更新父目录目录项
    if(!removeDirectoryEntry(parent_inode, dirname)){
        std::cerr << "[REMOVE ERROR] 父目录没有该目录目录项" << std::endl;
        return false;
    }
    
    // 从inode表中移除
    auto it = inode_table.find(path);
    if (it != inode_table.end()) {
        inode_table.erase(it);
    }

    // TODO 释放inode
    // metadata_manager->free_inode(inode_no);
    
    return true;
}

// @wbl
// 打开目录，返回DIR结构体指针
ZBSS_DIR* FileSystem::opendir(const std::string& path) {
    // 查找目录inode
    auto ino_num = get_inode_number(path);
    if (ino_num == -1) {
        return NULL; // 目录不存在
    }
    
    auto inode = std::make_shared<Inode>();
    metadata_manager->get_inode_storage()->read_inode(ino_num,*inode);
    
    // 检查是否是目录
    if (inode->file_mode.fields.file_type != static_cast<uint8_t>(FileType::Directory)) {
        return nullptr; // 不是目录
    }
    
    // 创建DIR结构体
    ZBSS_DIR* dir = new ZBSS_DIR;
    
    // 初始化DIR结构体
    dir->inode = inode;
    dir->volume = find_volume_by_inode(inode);
    dir->current_offset = 0;
    
    // 读取目录项
    dir->entries = readDirectoryEntries(inode);
    
    // 更新访问时间
    inode->setFaTime(InodeTimestamp());
    metadata_manager->get_inode_storage()->write_inode(inode->inode, *inode);
    
    return dir;
}

// @wbl
struct ZBSS_dirent* FileSystem::readdir(ZBSS_DIR* dirp) {
    if (!dirp || dirp->current_offset >= dirp->entries.size()) {
        return nullptr; // 目录结束或无效指针
    }
    
    // 获取当前目录项
    auto& entry = dirp->entries[dirp->current_offset++];
    
    // 填充dirent结构
    static struct ZBSS_dirent result;
    memset(&result, 0, sizeof(result));
    
    // 设置文件名
    strncpy(result.d_name, entry.name, sizeof(result.d_name) - 1);
    
    // 设置文件类型
    switch (entry.file_type) {
    case FileType::Regular:    result.d_type = static_cast<uint8_t>(DirEntryType::Regular); break;
    case FileType::Directory:  result.d_type = static_cast<uint8_t>(DirEntryType::Directory); break;
    case FileType::Symlink:    result.d_type = static_cast<uint8_t>(DirEntryType::Symlink); break;
    case FileType::BlockDev:   result.d_type = static_cast<uint8_t>(DirEntryType::BlockDev); break;
    case FileType::CharDev:    result.d_type = static_cast<uint8_t>(DirEntryType::CharDev); break;
    case FileType::Fifo:       result.d_type = static_cast<uint8_t>(DirEntryType::Fifo); break;
    case FileType::Socket:     result.d_type = static_cast<uint8_t>(DirEntryType::Socket); break;
    default:                  result.d_type = static_cast<uint8_t>(DirEntryType::Unknown);
    }
    
    return &result;
}

// @wbl
// 关闭目录，释放资源
int FileSystem::closedir(ZBSS_DIR* dirp) {
    if (!dirp) {
        return -1; // 无效指针
    }
    
    // 清理资源
    delete dirp;
    return 0;
}

bool FileSystem::persist_all_volumes(const std::vector<std::shared_ptr<Volume>>& volumes, const std::string& filename) {
	std::ofstream ofs(filename, std::ios::binary);
	if (!ofs) return false;

	uint32_t volume_count = static_cast<uint32_t>(volumes.size());
	ofs.write(reinterpret_cast<const char*>(&volume_count), sizeof(volume_count));

	// 预留每个卷长度的空间
	std::vector<uint32_t> volume_sizes(volume_count, 0);
	std::streampos size_table_pos = ofs.tellp();
	ofs.seekp(static_cast<std::streamoff>(sizeof(uint32_t)) * volume_count, std::ios::cur);

	// 写入每个卷内容并记录长度
	for (size_t i = 0; i < volume_count; ++i) {
		auto data = volumes[i]->serialize();
		volume_sizes[i] = static_cast<uint32_t>(data.size());
		ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	}

	// 回写卷长度表
	ofs.seekp(size_table_pos);
	ofs.write(reinterpret_cast<const char*>(volume_sizes.data()), static_cast<std::streamsize>(sizeof(uint32_t) * volume_count));

	return ofs.good();
}

std::vector<std::shared_ptr<Volume>> FileSystem::restore_all_volumes(const std::string& filename) {
	std::ifstream ifs(filename, std::ios::binary);
	std::vector<std::shared_ptr<Volume>> volumes;
	if (!ifs) return volumes;

	uint32_t volume_count = 0;
	ifs.read(reinterpret_cast<char*>(&volume_count), sizeof(volume_count));
	if (volume_count == 0) return volumes;

	std::vector<uint32_t> volume_sizes(volume_count);
	ifs.read(reinterpret_cast<char*>(volume_sizes.data()), static_cast<std::streamsize>(sizeof(uint32_t) * volume_count));

	for (uint32_t i = 0; i < volume_count; ++i) {
		std::vector<uint8_t> data(volume_sizes[i]);
		ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(volume_sizes[i]));
		auto vol = Volume::deserialize(data.data(), data.size());
		if (vol) volumes.push_back(std::move(vol));
	}
	return volumes;
}

bool FileSystem::persist_ssd_hdd_volumes(const std::string& ssd_filename, const std::string& hdd_filename) {
	bool ssd_ok = persist_all_volumes(ssd_volumes, ssd_filename);
	bool hdd_ok = persist_all_volumes(hdd_volumes, hdd_filename);
	return ssd_ok && hdd_ok;
}

bool FileSystem::restore_ssd_hdd_volumes(const std::string& ssd_filename, const std::string& hdd_filename) {
	auto ssd = restore_all_volumes(ssd_filename);
	auto hdd = restore_all_volumes(hdd_filename);
	if (ssd.empty() && hdd.empty()) return false;
	ssd_volumes = std::move(ssd);
	hdd_volumes = std::move(hdd);
	return true;
}

bool FileSystem::startup() {
	try {
		// 1. 加载卷信息（仅前3个）
		ssd_volumes.clear();
		hdd_volumes.clear();
		ssd_volume_indices.clear();
		hdd_volume_indices.clear();

		int ssd_total = get_persisted_ssd_volume_count(SSD_VOLUME_META_PATH);
		int hdd_total = get_persisted_hdd_volume_count(HDD_VOLUME_META_PATH);
		if (ssd_total < 0) ssd_total = 0;
		if (hdd_total < 0) hdd_total = 0;

		int ssd_to_load = std::min(3, ssd_total);
		int hdd_to_load = std::min(3, hdd_total);

		for (int i = 0; i < ssd_to_load; ++i) {
			load_nth_ssd_volume(static_cast<uint32_t>(i), SSD_VOLUME_META_PATH, SSD_VOLUME_DATA_PATH);
		}
		for (int i = 0; i < hdd_to_load; ++i) {
			load_nth_hdd_volume(static_cast<uint32_t>(i), HDD_VOLUME_META_PATH, HDD_VOLUME_DATA_PATH);
		}

		std::cout << "[FileSystem] 卷信息加载完成（按需加载），SSD卷数量: "
				  << ssd_volumes.size() << " / 总数 " << ssd_total
				  << "，HDD卷数量: " << hdd_volumes.size() << " / 总数 " << hdd_total << std::endl;

		std::cout << "[FileSystem] 卷信息加载完成，SSD卷数量: "
				  << ssd_volumes.size() << ", HDD卷数量: " << hdd_volumes.size() << std::endl;

		std::cout << "[FileSystem] Inode 分配信息加载完成，当前总 inode 数量: "
				  << metadata_manager->get_inode_storage()->size() / InodeStorage::INODE_DISK_SLOT_SIZE << std::endl;

		// 重建 inode 表（仅用于测试）
		rebuild_inode_table();

		// 3. 加载 StorageNode 信息
		g_storage_resource->loadFromFile(false, false, "/mnt/md0/node/storage_nodes.json");

		std::cout << "[FileSystem] StorageNode 信息加载完成 ";
		std::cout << "[FileSystem] 启动完成，系统信息已加载。" << std::endl;
		return true;
	} catch (const std::exception& e) {
		std::cerr << "[FileSystem] 启动失败: " << e.what() << std::endl;
		return false;
	}
}

bool FileSystem::shutdown() {
	try {
		// 1. 持久化卷信息（SSD/HDD）
		{
			// SSD
			int cur_count = get_persisted_ssd_volume_count(SSD_VOLUME_META_PATH);
			if (cur_count < 0) cur_count = 0;
			for (size_t i = 0; i < ssd_volumes.size(); ++i) {
				uint32_t idx;
				if (i < ssd_volume_indices.size()) {
					idx = ssd_volume_indices[i];
				} else {
					idx = static_cast<uint32_t>(cur_count); // 末尾追加
					++cur_count;
					ssd_volume_indices.push_back(idx);
				}
				if (!persist_ssd_volume_at(idx, ssd_volumes[i], SSD_VOLUME_META_PATH, SSD_VOLUME_DATA_PATH)) {
					std::cerr << "[FileSystem] 持久化SSD卷失败 index=" << idx << std::endl;
				}
			}
		}
		{
			// HDD
			int cur_count = get_persisted_hdd_volume_count(HDD_VOLUME_META_PATH);
			if (cur_count < 0) cur_count = 0;
			for (size_t i = 0; i < hdd_volumes.size(); ++i) {
				uint32_t idx;
				if (i < hdd_volume_indices.size()) {
					idx = hdd_volume_indices[i];
				} else {
					idx = static_cast<uint32_t>(cur_count); // 末尾追加
					++cur_count;
					hdd_volume_indices.push_back(idx);
				}
				if (!persist_hdd_volume_at(idx, hdd_volumes[i], HDD_VOLUME_META_PATH, HDD_VOLUME_DATA_PATH)) {
					std::cerr << "[FileSystem] 持久化HDD卷失败 index=" << idx << std::endl;
				}
			}
		}

		// 2. 持久化 inode 分配信息
		if (metadata_manager) {
			metadata_manager->save_bitmap();
		}

		// 3. 持久化 StorageNode 信息
		g_storage_resource->saveToFile("/mnt/md0/node/storage_nodes.json");

		std::cout << "[FileSystem] 关闭完成，系统信息已持久化。" << std::endl;
		return true;
	} catch (const std::exception& e) {
		std::cerr << "[FileSystem] 关闭失败: " << e.what() << std::endl;
		return false;
	}
}

void FileSystem::rebuild_inode_table() {
	inode_table.clear();
	if (!metadata_manager) return;
	auto inode_storage = metadata_manager->get_inode_storage();
	size_t inode_count = inode_storage->size() / InodeStorage::INODE_DISK_SLOT_SIZE;
	for (size_t i = 0; i < inode_count; ++i) {
		Inode inode;
		if (inode_storage->read_inode(i, inode)) {
			if (!inode.filename.empty()) {
				inode_table[inode.filename] = inode.inode;
			}
		}
	}
	std::cout << "[FileSystem] inode_table重建完成，文件数: " << inode_table.size() << std::endl;
}

uint64_t FileSystem::get_root_inode() const {
	return 2; // 根目录 inode 号暂定为 2
}

uint64_t FileSystem::get_inode_number(const std::string& abs_path) {
	// 先查找 inode_table
	auto it = inode_table.find(abs_path);
	if (it != inode_table.end()) {
		return it->second;
	}

	// 如果未找到，则使用路径解析查找
	auto inode_ptr = find_inode_by_path(abs_path);
	if (inode_ptr != nullptr) {
		return inode_ptr->inode;
	}

	return static_cast<uint64_t>(-1); // 未找到
}

