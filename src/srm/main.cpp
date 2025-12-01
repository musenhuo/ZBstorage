#include "storage_manager/StorageResource.h"
#include <cstdio>
#include <iostream>
#include <iomanip>
#include "../fs/volume/Volume.h"

// StorageResource 全局指针（在公共实现文件中定义）
extern StorageResource* g_storage_resource;

int main(){
    StorageResource resource;
    g_storage_resource = &resource; // 设置全局资源指针，供 Volume 使用

    // 尝试从文件加载（若无文件，可考虑调用 generateResource()）
    resource.loadFromFile();
    resource.printInfo();

    std::cout << "\n初始化并打印前 20 个卷的信息：\n";
    for (int i = 0; i < 20; ++i) {
        auto vols = resource.initOneNodeVolume(); // pair<shared_ptr<Volume>, shared_ptr<Volume>>
        std::cout << "[#" << (i+1) << "] ";

        auto print_vol = [&](const std::shared_ptr<Volume>& v, const char* tag) {
            if (!v) {
                std::cout << tag << ": <null>  ";
                return;
            }
            std::cout << tag << ": uuid=" << v->uuid()
                      << " node=" << v->storage_node_id()
                      << " total_blocks=" << v->total_blocks()
                      << " used=" << v->used_blocks()
                      << " usage=" << std::fixed << std::setprecision(2) << v->usage_percentage() << "%  ";
        };

        print_vol(vols.first, "SSD");
        print_vol(vols.second, "HDD");
        std::cout << std::endl;
    }

    return 0;
}

//功能性测试：1.十亿光盘资源管理 2.万级存储节点资源管理 3.光盘库节点管理 
//1.光盘资源可生成，可导入，可更改
//2.存储节点资源可生成，可导入，可初始化卷
//3.光盘库节点可生成，可导入，可分配光盘到库