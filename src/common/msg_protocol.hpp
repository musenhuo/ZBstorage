// common.hpp
#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint> // 为了使用 uint32_t 等固定大小整数
#include <any>

#include "cereal/cereal.hpp"         // Cereal 核心库
#include "cereal/types/string.hpp"   // 添加对 std::string 的序列化支持

enum class Command : uint16_t {
    Echo,
    Status,
    RegisterUser,
    RegisterReply,

    GetFileInfoRequest,
    GetFileInfoResponse
};

// 简单的消息结构
struct Message {
    std::string identity; // 用于 ROUTER 模式，标记消息来源/目的地
    Command command;  // 消息的命令类型
    std::any payload;  // 消息内容
};

// 定义一个用户信息结构体
struct UserInfo {
    uint32_t user_id;
    std::string username;
    std::string email;

    // Cereal 的序列化函数
    // 这是一个模板，无论是序列化还是反序列化都用这一个函数
    template<class Archive>
    void serialize(Archive & archive)
    {
        // 按照顺序，指明要序列化哪些成员变量
        archive( CEREAL_NVP(user_id), CEREAL_NVP(username), CEREAL_NVP(email) );
    }
};

// 线程安全的队列
template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));
        m_cond.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty() || m_stop; });
        if (m_queue.empty()) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cond.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_stop = false;
};