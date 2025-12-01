#pragma once
#include <string>
#include <cstdint>
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"

// 文件元数据结构体
struct FileInfo {
    std::string filename;
    uint64_t size_bytes = 0;
    uint64_t creation_timestamp = 0;
    
    template<class Archive>
    void serialize(Archive & archive) {
        archive( CEREAL_NVP(filename), CEREAL_NVP(size_bytes), CEREAL_NVP(creation_timestamp) );
    }
};