#pragma once
#include <vector>
#include <optional>
#include <zmq.hpp>
#include "common/msg_protocol.hpp" // 需要 Message 结构体

// 协议接口（抽象基类）
class IProtocol {
public:
    virtual ~IProtocol() = default;

    // 尝试从原始消息帧中解析出结构化的 Message
    // 如果解析失败（格式不符），返回 std::nullopt
    virtual std::optional<Message> parse_message(std::vector<zmq::message_t>& parts) = 0;

    // 将结构化的 Message 封装成待发送的 ZMQ 消息帧
    virtual std::vector<zmq::message_t> serialize_message(Message& msg) = 0;
};