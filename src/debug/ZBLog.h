#pragma once
#include <atomic>
#include <iostream>
#include <cstdlib>

namespace zbss {
enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Debug = 3 };

#ifdef ZBSS_ENABLE_LOG
extern std::atomic<int> g_log_level;

inline bool log_enabled(LogLevel lv) {
    return static_cast<int>(lv) <= g_log_level.load(std::memory_order_relaxed);
}

inline void set_log_level(LogLevel lv) {
    g_log_level.store(static_cast<int>(lv), std::memory_order_relaxed);
}

// 从环境变量读取：ZBSS_LOG_LEVEL=DEBUG|INFO|WARN|ERROR
inline void init_logging_from_env() {
    const char* env = std::getenv("ZBSS_LOG_LEVEL");
    if (!env) return;
    std::string s(env);
    for (auto& c : s) c = std::toupper(c);
    if (s == "DEBUG") set_log_level(LogLevel::Debug);
    else if (s == "INFO") set_log_level(LogLevel::Info);
    else if (s == "WARN" || s == "WARNING") set_log_level(LogLevel::Warn);
    else if (s == "ERROR") set_log_level(LogLevel::Error);
}
#else
// 关闭日志时，内联空实现，编译期消除开销
inline bool log_enabled(LogLevel) { return false; }
inline void set_log_level(LogLevel) {}
inline void init_logging_from_env() {}
#endif
} // namespace zbss

// 简单流式宏：用法 LOGD("open fd=" << fd)
#ifdef ZBSS_ENABLE_LOG
  #define LOGE(x) do { if (zbss::log_enabled(zbss::LogLevel::Error)) std::cerr << "[E] " << __FILE__ << ":" << __LINE__ << " " << x << std::endl; } while(0)
  #define LOGW(x) do { if (zbss::log_enabled(zbss::LogLevel::Warn))  std::cerr << "[W] " << __FILE__ << ":" << __LINE__ << " " << x << std::endl; } while(0)
  #define LOGI(x) do { if (zbss::log_enabled(zbss::LogLevel::Info))  std::cout << "[I] " << x << std::endl; } while(0)
  #define LOGD(x) do { if (zbss::log_enabled(zbss::LogLevel::Debug)) std::cout << "[D] " << x << std::endl; } while(0)
#else
  #define LOGE(x) do{}while(0)
  #define LOGW(x) do{}while(0)
  #define LOGI(x) do{}while(0)
  #define LOGD(x) do{}while(0)
#endif

// 使用示例
/* 
# 1. 创建/进入 tests 构建目录
mkdir -p /mnt/md0/Projects/ZBStorage/tests/build
cd /mnt/md0/Projects/ZBStorage/tests/build

# 2. 配置 cmake，启用 zbss 日志并设为 DEBUG 级别
cmake .. -DENABLE_ZBSS_LOG=ON -DDEFAULT_ZBSS_LOG_LEVEL=3

# 3. 构建单个测试目标（更快）
cmake --build . --target test_test_vfs_new -j

# 4. 运行测试并让 debug 级别输出生效
export ZBSS_LOG_LEVEL=DEBUG
./test_test_vfs_new
*/