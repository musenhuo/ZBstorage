#pragma once
#include <zmq.hpp>
#include <thread>
#include <memory>
#include <atomic>
#include <vector>
#include <chrono>
#include "common/msg_protocol.hpp"
#include "protocol_interface.hpp"

class Messenger {
public:
    Messenger(zmq::context_t& context, zmq::socket_type type)
        : m_socket(context, type) {}

    ~Messenger() {
        stop();
    }

    void bind(const std::string& addr) { m_socket.bind(addr); }
    void connect(const std::string& addr) { m_socket.connect(addr); }
    
    void start() { m_thread = std::thread(&Messenger::io_loop, this); }
    
    void stop() {
        if (m_stop_flag.exchange(true)) return;
        m_outgoing_queue.stop();
        m_incoming_queue.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    
    void send_message(Message msg);
    
    bool recv_raw_message(std::vector<zmq::message_t>& parts) {
        return m_incoming_queue.pop(parts);
    }
    
    void set_protocol(std::shared_ptr<IProtocol> protocol) {
        m_protocol = protocol;
    }

private:
    void io_loop();
    void handle_recv();
    void handle_send();
    void drain_outgoing_queue();

private:
    zmq::socket_t m_socket;
    std::thread m_thread;
    std::atomic<bool> m_stop_flag{false};
    std::shared_ptr<IProtocol> m_protocol;

    ThreadSafeQueue<std::vector<zmq::message_t>> m_incoming_queue;
    ThreadSafeQueue<Message> m_outgoing_queue;
    std::queue<std::vector<zmq::message_t>> m_outgoing_queue_internal;
};

// --- 实现部分 ---

inline void Messenger::send_message(Message msg) {
    m_outgoing_queue.push(std::move(msg));
}

inline void Messenger::drain_outgoing_queue() {
    if (!m_protocol) return;

    Message msg;
    while (m_outgoing_queue.try_pop(msg)) {
        auto parts = m_protocol->serialize_message(msg);
        if (!parts.empty()) { // 增加一个检查，避免发送空消息
            m_outgoing_queue_internal.push(std::move(parts));
        }
    }
}

inline void Messenger::io_loop() {
    while (!m_stop_flag) {
        zmq::pollitem_t items[] = {
            { m_socket, 0, ZMQ_POLLIN, 0 },
            { m_socket, 0, ZMQ_POLLOUT, 0 }
        };

        if (m_outgoing_queue_internal.empty()) {
            drain_outgoing_queue();
        }

        int pollin_events = 1;
        int poll_timeout = 100;

        if (m_outgoing_queue_internal.empty()) {
            zmq::poll(items, pollin_events, std::chrono::milliseconds(poll_timeout));
        } else {
            zmq::poll(items, 2, std::chrono::milliseconds(poll_timeout));
        }

        if (items[0].revents & ZMQ_POLLIN) { handle_recv(); }
        if (items[1].revents & ZMQ_POLLOUT) { handle_send(); }
    }
}

// <--- 核心修正点 1：手动实现 recv_multipart ---
inline void Messenger::handle_recv() {
    std::vector<zmq::message_t> recv_parts;
    zmq::message_t part;
    
    // 循环接收消息的每一部分
    while (m_socket.recv(part, zmq::recv_flags::none)) {
        recv_parts.push_back(std::move(part));
        // 检查是否还有更多部分
        if (!m_socket.get(zmq::sockopt::rcvmore)) {
            break; // 这是最后一部分，退出循环
        }
    }

    if (!recv_parts.empty()) {
        m_incoming_queue.push(std::move(recv_parts));
    }
}

// <--- 核心修正点 2：手动实现 send_multipart ---
inline void Messenger::handle_send() {
    if (m_outgoing_queue_internal.empty()) return;
    
    std::vector<zmq::message_t>& parts = m_outgoing_queue_internal.front();
    
    // 循环发送消息的每一部分
    for (size_t i = 0; i < parts.size(); ++i) {
        // 对于除最后一部分外的所有部分，都使用 sndmore 标志
        auto flag = (i < parts.size() - 1) ? zmq::send_flags::sndmore : zmq::send_flags::none;
        m_socket.send(parts[i], flag);
    }
    
    m_outgoing_queue_internal.pop();
}