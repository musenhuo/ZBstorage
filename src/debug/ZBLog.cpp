#include "ZBLog.h"
#include <atomic>

namespace zbss {
#ifdef ZBSS_ENABLE_LOG
#ifndef ZBSS_DEFAULT_LOG_LEVEL
// 默认 INFO；可在编译时 -DZBSS_DEFAULT_LOG_LEVEL=3 设为 DEBUG
#define ZBSS_DEFAULT_LOG_LEVEL 2
#endif
std::atomic<int> g_log_level{ ZBSS_DEFAULT_LOG_LEVEL };
#endif
} // namespace zbss