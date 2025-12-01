#include "InodeTimestamp.h"

namespace {

uint32_t encode_year_offset(int full_year) {
    int offset = full_year - 2000;
    if (offset < 0) offset = 0;
    if (offset > 255) offset = 255;
    return static_cast<uint32_t>(offset);
}

int decode_year_offset(uint32_t stored) {
    return 2000 + static_cast<int>(stored & 0xFF);
}

} // namespace

InodeTimestamp::InodeTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    int full_year = now->tm_year + 1900;
    year   = encode_year_offset(full_year);
    month  = now->tm_mon + 1;
    day    = now->tm_mday;
    hour   = now->tm_hour;
    minute = now->tm_min;
}

void InodeTimestamp::print() const {
    std::cout << decode_year_offset(year) << "/"
              << static_cast<int>(month) << "/"
              << static_cast<int>(day) << " "
              << static_cast<int>(hour) << ":"
              << static_cast<int>(minute) << std::endl;
}