#include <iostream>
#include <memory>
#include <vector>
#include "../src/srm/optical_manager/DiscManager.h"

int main() {
    // DiscManager manager;

    // // 构造几张光盘并添加到管理器
    // std::vector<std::shared_ptr<OpticalDisc>> discs;
    // discs.push_back(std::make_shared<OpticalDisc>("disc_0000000000", "lib_00000", OPTICAL_DISC_CAPACITY, OPTICAL_DISC_WRITE_MBPS, OPTICAL_DISC_READ_MBPS));
    // discs.push_back(std::make_shared<OpticalDisc>("disc_0000000001", "lib_00000", OPTICAL_DISC_CAPACITY, OPTICAL_DISC_WRITE_MBPS, OPTICAL_DISC_READ_MBPS));
    // discs.push_back(std::make_shared<OpticalDisc>("disc_0000000002", "lib_00001", OPTICAL_DISC_CAPACITY, OPTICAL_DISC_WRITE_MBPS, OPTICAL_DISC_READ_MBPS));

    // for (const auto& disc : discs) {
    //     manager.addDisc(disc, DiscStatus::Blank);
    //     std::cout << "[INFO] 添加光盘 " << disc->device_id
    //               << " 当前空闲数量: " << manager.blankDiscCount()
    //               << " 总数: " << manager.totalDiscCount() << std::endl;
    // }

    // // 将第二张光盘标记为使用中
    // manager.setDiscStatus(discs[1], DiscStatus::InUse);
    // std::cout << "[INFO] 设置 " << discs[1]->device_id << " 为 InUse"
    //           << " | 空闲: " << manager.blankDiscCount()
    //           << " | 使用中: " << manager.inuseDiscCount() << std::endl;

    // // 将第三张光盘标记为已完成
    // manager.setDiscStatus(discs[2], DiscStatus::Finalized);
    // std::cout << "[INFO] 设置 " << discs[2]->device_id << " 为 Finalized"
    //           << " | 已完成: " << manager.finalizedDiscCount() << std::endl;

    // // 回收第一张光盘
    // manager.recycleDisc(discs[0]->device_id);
    // std::cout << "[INFO] 回收光盘 " << discs[0]->device_id
    //           << " | 回收数量: " << manager.recycledDiscCount() << std::endl;

    // // 尝试查找已添加的光盘
    // auto found = manager.findDisc("disc_0000000001");
    // if (found) {
    //     std::cout << "[INFO] 查找到光盘: " << found->device_id
    //               << " 状态: " << static_cast<int>(found->status) << std::endl;
    // } else {
    //     std::cout << "[WARN] 未找到指定光盘" << std::endl;
    // }

    // std::cout << "[RESULT] DiscManager 状态汇总 ->"
    //           << " 总数: " << manager.totalDiscCount()
    //           << " 空闲: " << manager.blankDiscCount()
    //           << " 使用中: " << manager.inuseDiscCount()
    //           << " 已完成: " << manager.finalizedDiscCount()
    //           << " 回收: " << manager.recycledDiscCount()
    //           << " 丢失: " << manager.lostDiscCount() << std::endl;

    return 0;
}
