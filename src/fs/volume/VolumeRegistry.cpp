#include "VolumeRegistry.h"
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <stdexcept>

// 简单文件名（可改为配置/构造参数）
static const char* kSSDMeta = "ssd.meta";
static const char* kSSDData = "ssd.data";
static const char* kHDDMeta = "hdd.meta";
static const char* kHDDData = "hdd.data";

namespace {
    // 从 Volume 序列化数据中提取 uuid（不依赖 Volume 的 getter）
    static std::string extract_uuid_from_serialized(const std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uint16_t)) return {};
        uint16_t uuid_len = 0;
        std::memcpy(&uuid_len, buf.data(), sizeof(uuid_len));
        if (sizeof(uint16_t) + uuid_len > buf.size()) return {};
        return std::string(reinterpret_cast<const char*>(buf.data() + sizeof(uint16_t)), uuid_len);
    }
    static std::string extract_uuid_from_volume(const std::shared_ptr<Volume>& v) {
        auto data = v->serialize();
        return extract_uuid_from_serialized(data);
    }
}

class FileVolumeRegistry final : public IVolumeRegistry {
private:
    std::string base_dir_;                                     ///< 元数据文件与数据文件所在根目录
    std::string ssd_meta_path_, ssd_data_path_;                 ///< SSD 卷元数据/数据文件路径
    std::string hdd_meta_path_, hdd_data_path_;                 ///< HDD 卷元数据/数据文件路径
    mutable std::mutex mtx_;                                   ///< 保护内部状态的互斥量

    std::vector<std::shared_ptr<Volume>> ssd_;                  ///< 内存中的 SSD 卷列表
    std::vector<std::shared_ptr<Volume>> hdd_;                  ///< 内存中的 HDD 卷列表
    std::unordered_map<std::string, std::shared_ptr<Volume>> by_uuid_ssd_; ///< SSD 卷 uuid→卷实例映射
    std::unordered_map<std::string, std::shared_ptr<Volume>> by_uuid_hdd_; ///< HDD 卷 uuid→卷实例映射
    
public:
    explicit FileVolumeRegistry(std::string base_dir = ".")
        : base_dir_(std::move(base_dir)),
          ssd_meta_path_(base_dir_ + "/" + kSSDMeta),
          ssd_data_path_(base_dir_ + "/" + kSSDData),
          hdd_meta_path_(base_dir_ + "/" + kHDDMeta),
          hdd_data_path_(base_dir_ + "/" + kHDDData) {}

    bool register_volume(const std::shared_ptr<Volume>& vol,
                         VolumeType type,
                         int* out_index,
                         bool persist_now) override {
        std::lock_guard<std::mutex> lk(mtx_);
        auto uuid = extract_uuid_from_volume(vol);
        if (uuid.empty()) return false;

        auto& list = (type == VolumeType::SSD) ? ssd_ : hdd_;
        auto& meta_path = (type == VolumeType::SSD) ? ssd_meta_path_ : hdd_meta_path_;
        auto& data_path = (type == VolumeType::SSD) ? ssd_data_path_ : hdd_data_path_;
        auto& uuid_map = (type == VolumeType::SSD) ? by_uuid_ssd_ : by_uuid_hdd_;

        // 去重
        if (uuid_map.count(uuid)) return true;

        int index = static_cast<int>(list.size());
        list.push_back(vol);
        uuid_map.emplace(uuid, vol);
        if (out_index) *out_index = index;

        if (!persist_now) return true;

        // 追加持久化：data 追加序列化数据，meta 追加新前缀并更新 count
        if (!ensure_meta_initialized(meta_path)) return false;

        uint32_t old_count = 0;
        uint64_t last_prefix = 0;
        if (!read_meta_last_prefix(meta_path, old_count, last_prefix)) return false;

        auto payload = vol->serialize();
        std::ofstream data(data_path, std::ios::binary | std::ios::out | std::ios::app);
        if (!data.is_open()) return false;
        data.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        data.flush();
        data.close();

        uint64_t new_prefix = last_prefix + payload.size();
        if (!append_meta_prefix(meta_path, new_prefix, old_count + 1)) return false;

        return true;
    }

    std::shared_ptr<Volume> find_by_uuid(const std::string& uuid) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (auto it = by_uuid_ssd_.find(uuid); it != by_uuid_ssd_.end()) return it->second;
        if (auto it = by_uuid_hdd_.find(uuid); it != by_uuid_hdd_.end()) return it->second;
        return nullptr;
    }

    const std::vector<std::shared_ptr<Volume>>& list(VolumeType type) const override {
        return (type == VolumeType::SSD) ? ssd_ : hdd_;
    }

    bool startup() override {
        std::lock_guard<std::mutex> lk(mtx_);
        // SSD
        if (!ensure_meta_initialized(ssd_meta_path_)) return false;
        if (!load_all(ssd_meta_path_, ssd_data_path_, ssd_, by_uuid_ssd_)) return false;
        // HDD
        if (!ensure_meta_initialized(hdd_meta_path_)) return false;
        if (!load_all(hdd_meta_path_, hdd_data_path_, hdd_, by_uuid_hdd_)) return false;
        return true;
    }

    bool shutdown() override {
        // 采用 append 持久化，shutdown 无需重写全量；这里保留接口
        return true;
    }

private:
    /**
     * @brief 确保元数据文件存在，若不存在则创建并写入计数 0。
     * @param meta_filename 元数据文件路径。
     * @return 操作是否成功。
     */
    bool ensure_meta_initialized(const std::string& meta_filename) const {
        std::fstream f(meta_filename, std::ios::in | std::ios::binary);
        if (f.is_open()) return true;
        // 不存在则创建并写入 count=0
        std::ofstream out(meta_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        uint32_t zero = 0;
        out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        out.flush();
        return true;
    }

    /**
     * @brief 读取元数据文件中的卷数量。
     * @param meta_filename 元数据文件路径。
     * @return 卷数量，失败时返回 -1。
     */
    int get_volume_count_core(const std::string& meta_filename) const {
        std::ifstream in(meta_filename, std::ios::binary);
        if (!in.is_open()) return -1;
        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in.good()) return -1;
        return static_cast<int>(count);
    }

    /**
     * @brief 读取元数据文件中的最后一个前缀和卷数量。
     * @param meta_filename 元数据文件路径。
     * @param out_count 输出：卷数量。
     * @param out_last_prefix 输出：最后写入的前缀偏移。
     * @return 成功返回 true，否则 false。
     */
    bool read_meta_last_prefix(const std::string& meta_filename,
                               uint32_t& out_count,
                               uint64_t& out_last_prefix) const {
        std::ifstream in(meta_filename, std::ios::binary);
        if (!in.is_open()) return false;
        in.read(reinterpret_cast<char*>(&out_count), sizeof(out_count));
        if (!in.good()) return false;
        if (out_count == 0) {
            out_last_prefix = 0;
            return true;
        }
        // 定位到最后一个前缀
        in.seekg(sizeof(out_count) + static_cast<std::streamoff>((out_count - 1) * sizeof(uint64_t)), std::ios::beg);
        in.read(reinterpret_cast<char*>(&out_last_prefix), sizeof(out_last_prefix));
        return in.good();
    }

    /**
     * @brief 追加新前缀并更新卷数量。
     * @param meta_filename 元数据文件路径。
     * @param new_prefix 新的前缀偏移。
     * @param new_count 更新后的卷数量。
     * @return 成功返回 true，否则 false。
     */
    bool append_meta_prefix(const std::string& meta_filename,
                            uint64_t new_prefix,
                            uint32_t new_count) const {
        // 先在末尾追加前缀，再回到文件头更新 count
        std::fstream f(meta_filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!f.is_open()) return false;

        f.seekp(0, std::ios::end);
        f.write(reinterpret_cast<const char*>(&new_prefix), sizeof(new_prefix));
        if (!f.good()) return false;

        f.seekp(0, std::ios::beg);
        f.write(reinterpret_cast<const char*>(&new_count), sizeof(new_count));
        f.flush();
        return f.good();
    }

    /**
     * @brief 读取指定索引卷的前缀区间。
     * @param meta_filename 元数据文件路径。
     * @param index 目标卷索引。
     * @param out_count 输出：总卷数量。
     * @param out_prev_prefix 输出：该卷区间起始前缀（前一个前缀）。
     * @param out_cur_prefix 输出：该卷区间结束前缀。
     * @return 成功返回 true，否则 false。
     */
    bool read_meta_prefix_pair(const std::string& meta_filename,
                               uint32_t index,
                               uint32_t& out_count,
                               uint64_t& out_prev_prefix,
                               uint64_t& out_cur_prefix) const {
        std::ifstream in(meta_filename, std::ios::binary);
        if (!in.is_open()) return false;
        in.read(reinterpret_cast<char*>(&out_count), sizeof(out_count));
        if (!in.good()) return false;
        if (index >= out_count) return false;

        // 读取当前前缀
        in.seekg(sizeof(out_count) + static_cast<std::streamoff>(index * sizeof(uint64_t)), std::ios::beg);
        in.read(reinterpret_cast<char*>(&out_cur_prefix), sizeof(out_cur_prefix));
        if (!in.good()) return false;

        if (index == 0) {
            out_prev_prefix = 0;
            return true;
        }
        in.seekg(sizeof(out_count) + static_cast<std::streamoff>((index - 1) * sizeof(uint64_t)), std::ios::beg);
        in.read(reinterpret_cast<char*>(&out_prev_prefix), sizeof(out_prev_prefix));
        return in.good();
    }

    /**
     * @brief 载入并反序列化指定索引的卷。
     * @param meta_filename 元数据文件路径。
     * @param data_filename 数据文件路径。
     * @param index 目标卷索引。
     * @param out_list 输出：卷列表。
     * @param uuid_map 输出：uuid 到卷的映射。
     * @return 成功返回 true，否则 false。
     */
    bool load_nth_volume_core(const std::string& meta_filename,
                              const std::string& data_filename,
                              uint32_t index,
                              std::vector<std::shared_ptr<Volume>>& out_list,
                              std::unordered_map<std::string, std::shared_ptr<Volume>>& uuid_map) const {
        uint32_t count = 0;
        uint64_t prev = 0, cur = 0;
        if (!read_meta_prefix_pair(meta_filename, index, count, prev, cur)) return false;
        if (cur < prev) return false;
        uint64_t sz = cur - prev;
        if (sz == 0) return false;

        std::ifstream data(data_filename, std::ios::binary);
        if (!data.is_open()) return false;
        data.seekg(static_cast<std::streamoff>(prev), std::ios::beg);

        std::vector<uint8_t> buf(static_cast<size_t>(sz));
        data.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
        if (!data.good()) return false;

        auto vol_ptr = Volume::deserialize(buf.data(), buf.size());
        if (!vol_ptr) return false;
        auto sp = std::shared_ptr<Volume>(std::move(vol_ptr));

        // 建立 uuid 索引
        auto uuid = extract_uuid_from_serialized(buf);
        if (!uuid.empty()) uuid_map.emplace(uuid, sp);
        out_list.push_back(std::move(sp));
        return true;
    }

    /**
     * @brief 加载所有卷到内存列表。
     * @param meta_filename 元数据文件路径。
     * @param data_filename 数据文件路径。
     * @param out_list 输出：卷列表。
     * @param uuid_map 输出：uuid 到卷的映射。
     * @return 成功返回 true，否则 false。
     */
    bool load_all(const std::string& meta_filename,
                  const std::string& data_filename,
                  std::vector<std::shared_ptr<Volume>>& out_list,
                  std::unordered_map<std::string, std::shared_ptr<Volume>>& uuid_map) const {
        out_list.clear();
        uuid_map.clear();

        int cnt = get_volume_count_core(meta_filename);
        if (cnt < 0) return false;
        for (int i = 0; i < cnt; ++i) {
            if (!load_nth_volume_core(meta_filename, data_filename, static_cast<uint32_t>(i), out_list, uuid_map)) {
                // 跳过坏条目并继续
                continue;
            }
        }
        return true;
    }
};

// 如果你需要在别处 new
std::shared_ptr<IVolumeRegistry> make_file_volume_registry(const std::string& base_dir) {
    return std::make_shared<FileVolumeRegistry>(base_dir);
}