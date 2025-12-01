#pragma once
#include <iostream>
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include "messenger.hpp"
#include "protocol_interface.hpp"

using MessageHandler = std::function<void(Message&, Messenger&)>;

class Dispatcher {
public:
    // --- 核心改动：构造函数接收 Messenger 和 Protocol ---
    Dispatcher(std::unique_ptr<Messenger> msgr, std::shared_ptr<IProtocol> protocol)
        : m_messenger(std::move(msgr)), m_protocol(std::move(protocol)) {
        // 让 Messenger 也持有 Protocol，以便发送消息时序列化
        m_messenger->set_protocol(m_protocol);
    }

    ~Dispatcher() {
        stop();
    }

    // --- 修改：register_handler 的 key 从 string 改为 Command ---
    void register_handler(Command command, MessageHandler handler) {
        m_handlers[command] = handler;
    }
    
    void start() {
        m_messenger->start();
        m_dispatch_thread = std::thread(&Dispatcher::dispatch_loop, this);
    }
    
    void stop() {
        if (m_stop_flag.exchange(true)) return;
        if (m_dispatch_thread.joinable()) {
            m_dispatch_thread.join();
        }
        m_messenger->stop();
    }

private:
    void dispatch_loop() {
        while (!m_stop_flag) {
            std::vector<zmq::message_t> parts;
            // --- 核心改动：从 Messenger 获取原始消息，然后用 Protocol 解析 ---
            if (m_messenger->recv_raw_message(parts)) {
                auto opt_msg = m_protocol->parse_message(parts);

                if (opt_msg) {
                    Message& msg = *opt_msg;
                    auto it = m_handlers.find(msg.command);
                    if (it != m_handlers.end()) {
                        try {
                            it->second(msg, *m_messenger);
                        } catch (const std::exception& e) {
                            std::cerr << "Handler for command '" << static_cast<uint16_t>(msg.command)
                                      << "' threw an exception: " << e.what() << std::endl;
                        }
                    } else {
                        // 修复笔误：msg_type -> msg.command
                        std::cerr << "No handler for message command: " << static_cast<uint16_t>(msg.command) << std::endl;
                    }
                } else {
                    std::cerr << "Failed to parse incoming message." << std::endl;
                }
            }
        }
    }

private:
    std::unique_ptr<Messenger> m_messenger;
    std::shared_ptr<IProtocol> m_protocol; // 持有协议
    std::map<Command, MessageHandler> m_handlers;
    std::thread m_dispatch_thread;
    std::atomic<bool> m_stop_flag{false};
};