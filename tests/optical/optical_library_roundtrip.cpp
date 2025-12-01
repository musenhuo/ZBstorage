#include "srm/storage_manager/StorageResource.h"
#include "storagenode/optical/OpticalDiscLibrary.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string make_temp_filename(const std::string& stem) {
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path();
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path path = tmp_dir / (stem + std::to_string(timestamp) + ".json");
    return path.string();
}

std::vector<std::shared_ptr<OpticalDiscLibrary>> build_sample_libraries() {
    auto lib_a = std::make_shared<OpticalDiscLibrary>("lib_00000", 5, 3, 1.5);
    lib_a->miss_slots = {1, 3, 5};
    lib_a->non_default_discs[42] = "disc_0004200000";

    auto lib_b = std::make_shared<OpticalDiscLibrary>("lib_00001", 2, 2, 0.8);
    lib_b->miss_slots = {0};
    lib_b->non_default_discs[17] = "disc_0001700001";
    lib_b->non_default_discs[23] = "disc_0002300001";

    return {lib_a, lib_b};
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    StorageResource original;
    auto libraries = build_sample_libraries();
    for (const auto& lib : libraries) {
        original.libraries.push_back(lib);
    }

    const std::string nodes_json = make_temp_filename("opt_nodes_");
    const std::string libs_json  = make_temp_filename("opt_libs_");

    original.saveToFile(nodes_json, libs_json);

    StorageResource roundtrip;
    roundtrip.loadFromFile(false, false, nodes_json, libs_json);
    assert(roundtrip.libraries.size() == libraries.size());

    for (size_t i = 0; i < libraries.size(); ++i) {
        const auto& expected = libraries[i];
        const auto& actual   = roundtrip.libraries[i];
        assert(actual->library_id == expected->library_id);
        assert(actual->drive_count == expected->drive_count);
        assert(actual->disc_num == expected->disc_num);
        assert(actual->miss_slots == expected->miss_slots);
        assert(actual->non_default_discs == expected->non_default_discs);
    }

    // 修改载入的光盘库信息，再次保存、读取并验证。
    roundtrip.libraries[0]->miss_slots.push_back(99);
    roundtrip.libraries[1]->non_default_discs[88] = "disc_0008800002";
    roundtrip.saveToFile(nodes_json, libs_json);

    StorageResource verify;
    verify.loadFromFile(false, false, nodes_json, libs_json);

    assert(verify.libraries.size() == 2);
    assert(!verify.libraries[0]->miss_slots.empty());
    assert(verify.libraries[0]->miss_slots.back() == 99);
    auto it = verify.libraries[1]->non_default_discs.find(88);
    assert(it != verify.libraries[1]->non_default_discs.end());
    assert(it->second == "disc_0008800002");

    std::cout << "Optical library round-trip test passed" << std::endl;

    // 清理生成的临时文件
    for (const auto& path : {nodes_json, libs_json}) {
        std::error_code ec;
        fs::remove(path, ec);
    }

    return 0;
}
