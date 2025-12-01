#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/storagenode/StorageNode.h"
#include "../src/storagenode/hard_disc/SSD.h"
#include "../src/storagenode/hard_disc/HDD.h"
#include "../src/fs/block/BlockManager.h"
#include "../src/msg/IO.h"
#include "../src/fs/volume/Volume.h"

int main() {
    using namespace std;

    // 构造一个 SSD 类型的存储节点
        StorageNode ssd_node("test_ssd_node", StorageNodeType::SSD);
        cout << "[INFO] Created SSD node '" << ssd_node.node_id << "' with "
            << ssd_node.ssd_devices.size() << " SSD devices and "
            << ssd_node.hdd_devices.size() << " HDD devices" << endl;
    assert(ssd_node.ssd_devices.size() == ssd_node.ssd_device_count);
    assert(ssd_node.hdd_devices.empty());

    ssd_node.initVolumes();
        cout << "[INFO] Initialized SSD volume with "
            << ssd_node.ssd_volume->total_blocks() << " blocks" << endl;
    assert(ssd_node.ssd_volume != nullptr);
    assert(ssd_node.hdd_volume == nullptr);
    assert(ssd_node.ssd_volume->total_blocks() > 0);

    // 写入一个块并读取，验证 processIO 返回正值
    vector<uint8_t> buffer(BLOCK_SIZE, 0xAB);
    IORequest write_req(
        IOType::Write,
        ssd_node.node_id,
        ssd_node.ssd_volume->uuid(),
        0,
        1,
        0,
        buffer.size(),
        buffer.data(),
        buffer.size()
    );
    double write_time = ssd_node.processIO(write_req);
    cout << "[INFO] Write IO completed in " << write_time << " ms" << endl;
    assert(write_time > 0.0);

    IORequest read_req(
        IOType::Read,
        ssd_node.node_id,
        ssd_node.ssd_volume->uuid(),
        0,
        1,
        0,
        buffer.size(),
        buffer.data(),
        buffer.size()
    );
    double read_time = ssd_node.processIO(read_req);
    cout << "[INFO] Read IO completed in " << read_time << " ms" << endl;
    assert(read_time > 0.0);

    // 验证 addDevice 对 HDD 的支持
    size_t before_hdd = ssd_node.hdd_devices.size();
    auto extra_hdd = std::make_shared<HardDiskDrive>(
        "extra_hdd",
        HDD_DEFAULT_CAPACITY,
        HDD_DEFAULT_WRITE_MBPS,
        HDD_DEFAULT_READ_MBPS
    );
        ssd_node.addDevice(extra_hdd);
        cout << "[INFO] Added extra HDD. Total HDD devices: "
            << ssd_node.hdd_devices.size() << endl;
    assert(ssd_node.hdd_devices.size() == before_hdd + 1);

    // 构造混合节点并初始化卷
    StorageNode mix_node("test_mix_node", StorageNodeType::Mix);
        cout << "[INFO] Created Mix node with " << mix_node.ssd_devices.size()
            << " SSDs and " << mix_node.hdd_devices.size() << " HDDs" << endl;
    assert(!mix_node.ssd_devices.empty());
    assert(!mix_node.hdd_devices.empty());
    mix_node.initVolumes();
        cout << "[INFO] Mix node volumes initialized. SSD blocks: "
            << mix_node.ssd_volume->total_blocks()
            << ", HDD blocks: " << mix_node.hdd_volume->total_blocks() << endl;
    assert(mix_node.ssd_volume != nullptr);
    assert(mix_node.hdd_volume != nullptr);

    cout << "StorageNode basic tests passed" << endl;
    return 0;
}
