// 简单的光盘库测试（非 gtest）：验证 add/has/remove/burn/to_json 等基本行为
#include <iostream>
#include <cassert>
#include <string>
#include "../src/storagenode/optical/OpticalDiscLibrary.h"

int main() {
    using namespace std;

    // 创建一个库，id 格式需要以 "lib_" 开头，选择库号 0
    OpticalDiscLibrary lib("lib_00000", OPTICAL_LIBRARY_DISC_NUM, 4, 0.5);

    // 准备用于测试的盘号（保证 id_num < OPTICAL_LIBRARY_DISC_NUM，使其属于库 0）
    int slot = 1234;
    string disc_id = string("disc_") + string(10 - to_string(slot).size(), '0') + to_string(slot);

    // 初始时，hasDisc 应返回槽位（默认光盘）或 -1（如果我们把它标为 miss）
    // 将该槽标记为 miss 再测试 addDisc 能否把它恢复
    lib.miss_slots.push_back(slot);
    int before = lib.hasDisc(disc_id);
    assert(before == -1 && "Expected not found when slot is in miss_slots");

    // 添加该光盘，应该移除 miss 并使 hasDisc 返回槽号
    lib.addDisc(disc_id);
    int after = lib.hasDisc(disc_id);
    if (after < 0) {
        cerr << "addDisc did not place disc into library as expected" << endl;
        return 2;
    }
    assert(after == slot);

    // 烧录一个小镜像，应该返回大于装载时间的值
    uint64_t img_size = 1ULL * 1024 * 1024; // 1 MiB
    double burn_time = lib.burnToDisc(disc_id, img_size);
    assert(burn_time > lib.load_unload_time);

    // 移除光盘后应返回 false -> hasDisc -1
    bool removed = lib.removeDisc(disc_id);
    assert(removed == true);
    assert(lib.hasDisc(disc_id) == -1);

    // to_json 中应包含 miss_slots 的条目（我们刚刚移除后会加入）
    auto j = lib.to_json();
    if (!j.contains("miss_slots")) {
        cerr << "to_json missing miss_slots" << endl;
        return 3;
    }

    cout << "OpticalDiscLibrary basic tests passed" << endl;
    return 0;
}
