#pragma once
#include <memory>
#include <vector>
#include <string>
#include "msg/IO.h"  // IORequest 定义

class StorageNode;
class OpticalDiscLibrary;
class Volume;

class StorageResource {
public:
    std::vector<std::shared_ptr<StorageNode>> uninitialized_nodes; // 未初始化卷的节点
    std::vector<std::shared_ptr<StorageNode>> nodes; // 存储节点
    std::vector<std::shared_ptr<OpticalDiscLibrary>> libraries; // 新增光盘库集合

     // 添加结点
    void addNode(const std::shared_ptr<StorageNode>& node);
    // 新增：添加光盘库
    void addLibrary(const std::shared_ptr<OpticalDiscLibrary>& lib);

    int getUninitializedNodeCount() const { return uninitialized_nodes.size(); }

    // 返回卷的 shared_ptr，便于外部安全持有引用（SSD,HDD）
    std::pair<std::shared_ptr<Volume>, std::shared_ptr<Volume>> initOneNodeVolume();
     // 根据 node_id 查找节点
    std::shared_ptr<StorageNode> findNode(const std::string& node_id) const ;

     // IO处理函数
    double processIO(const IORequest& req);

    void generateResource();

    void printInfo() const;

    

    // 保存所有节点到一个文件
    void saveToFile(const std::string& filename1 = "/mnt/md0/node/node.json",const std::string& filename2 = "/mnt/md0/node/library.json") const;

    // 从文件恢复所有节点
    void loadFromFile(bool initvolumes = false,bool fresh = false, const std::string& filename1  = "/mnt/md0/node/node.json",const std::string& filename2 = "/mnt/md0/node/library.json");

private:
    // 游标式按 id 递增取未初始化节点
    size_t uninit_cursor_ = 0;          // 指向下一个待初始化的最小 id 节点
    bool   uninit_sorted_ = false;       // 标记未初始化集合是否已排序
    
    // 将未初始化节点按 id 排序（基于数值后缀 + 类型权重）并重置游标
    void sortUninitializedById();
    // 从 node_id 中解析数值后缀；失败返回 -1
    static long parseIdSuffixNumber(const std::string& node_id);
    // 将节点转为排序 rank：先数值后缀，再类型权重（SSD=0, HDD=1, Mix=2）
    static long long nodeRank(const std::shared_ptr<StorageNode>& node);
};
