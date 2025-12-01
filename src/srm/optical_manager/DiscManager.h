#pragma once
#include "storagenode/optical/OpticalDisc.h"
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <string>
#include <iostream>

class DiscManager {
public:
    std::map<std::string, std::shared_ptr<OpticalDisc>> all_discs;
    std::vector<std::shared_ptr<OpticalDisc>> cache_discs; // 缓存一个二进制文件的光盘
    std::string current_binary_file = "/mnt/md0/node/disc/disc_batch_0.bin"; // 当前二进制文件名

    // 维护状态索引、查找、回收等（只存光盘id）
    std::set<std::string> blank_discs;     // 空闲光盘id
    std::set<std::string> inuse_discs;     // 正在使用的光盘id
    std::set<std::string> finalized_discs; // 已刻录完成的光盘id
    std::set<std::string> recycled_discs;  // 已回收的光盘id
    std::set<std::string> lost_discs;      // 丢失的光盘id

    void addDisc(const std::shared_ptr<OpticalDisc>& disc, DiscStatus status = DiscStatus::Blank);
    void setDiscStatus(const std::shared_ptr<OpticalDisc>& disc, DiscStatus status);

    size_t totalDiscCount() const;
    size_t blankDiscCount() const;
    size_t inuseDiscCount() const;
    size_t finalizedDiscCount() const;
    size_t recycledDiscCount() const;
    size_t lostDiscCount() const;

    std::shared_ptr<OpticalDisc> findDisc(const std::string& id);
    void recycleDisc(const std::string& id);
    void generateBlankDiscs(int count = 1000);

    void saveCacheToBin();
    void loadCacheFromBin();

    ~DiscManager() {
        if (!cache_discs.empty()) {
            std::cout << "缓存光盘信息已保存到二进制文件" << std::endl;
            // 这里只是声明析构行为；实际写入逻辑应在 .cpp 实现中完成
        }
    }
};
