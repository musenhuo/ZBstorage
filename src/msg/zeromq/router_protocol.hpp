#pragma once
#include "protocol_interface.hpp"
#include <functional>
#include <map>
#include <sstream>
#include <typeindex>
#include <iostream>
#include "cereal/archives/binary.hpp"

class RouterProtocol : public IProtocol {
public:
    template<typename T>
    void register_type(Command command) {
        m_deserializers[command] = [](const std::string& binary_data) -> std::any {
            T obj;
            std::stringstream ss(binary_data);
            try {
                cereal::BinaryInputArchive archive(ss);
                archive(obj);
            } catch (const cereal::Exception& e) {
                std::cerr << "Cereal deserialization error: " << e.what() << std::endl;
                return std::any{}; 
            }
            return obj;
        };

        m_serializers.emplace(std::type_index(typeid(T)), [](const std::any& any_data) -> std::string {
            const T& obj = std::any_cast<const T&>(any_data);
            std::stringstream ss;
            cereal::BinaryOutputArchive archive(ss);
            archive(obj);
            return ss.str();
        });

        m_command_to_type.emplace(command, typeid(T));
    }

    std::optional<Message> parse_message(std::vector<zmq::message_t>& parts) override {
        if (parts.size() < 3) return std::nullopt;

        Message msg;
        msg.identity = parts[0].to_string();

        if (parts[2].size() != sizeof(Command)) {
            std::cerr << "Error: Invalid command frame size." << std::endl;
            return std::nullopt;
        }
        uint16_t command_value;
        memcpy(&command_value, parts[2].data(), sizeof(command_value));
        msg.command = static_cast<Command>(command_value);

        auto it = m_deserializers.find(msg.command);
        if (it != m_deserializers.end()) {
            if (parts.size() > 3) {
                try {
                    std::string binary_payload = parts[3].to_string();
                    msg.payload = it->second(binary_payload);
                    if (!msg.payload.has_value()) return std::nullopt; 
                } catch (const std::exception& e) {
                    std::cerr << "Error: Deserialization failed for command. Reason: " 
                              << e.what() << std::endl;
                    return std::nullopt;
                }
            }
        }
        return msg;
    }

    std::vector<zmq::message_t> serialize_message(Message& msg) override {
        // --- 类型检查逻辑保持不变 ---
        if (msg.payload.has_value()) {
            auto it_type_check = m_command_to_type.find(msg.command);
            if (it_type_check != m_command_to_type.end()) {
                if (it_type_check->second != std::type_index(msg.payload.type())) {
                    std::cerr << "Error: Type mismatch for command '" << static_cast<uint16_t>(msg.command)
                              << "'. Message rejected." << std::endl;
                    return {};
                }
            } else {
                 std::cerr << "Warning: Command '" << static_cast<uint16_t>(msg.command) << "' has no registered payload type." << std::endl;
            }
        }
        
        std::vector<zmq::message_t> parts;

        // --- 关键修正：只有当 identity 不为空时（即服务器回复时），才添加 identity 和空分隔符 ---
        if (!msg.identity.empty()) {
            parts.emplace_back(zmq::message_t(msg.identity));
            parts.emplace_back(zmq::message_t());
        }

        // 命令和 payload 总是需要添加的
        auto command_value = static_cast<uint16_t>(msg.command);
        parts.emplace_back(zmq::message_t(&command_value, sizeof(command_value)));

        if (msg.payload.has_value()) {
            auto it_serializer = m_serializers.find(std::type_index(msg.payload.type()));
            if (it_serializer != m_serializers.end()) {
                std::string binary_payload = it_serializer->second(msg.payload);
                parts.emplace_back(zmq::message_t(binary_payload));
            } else {
                 std::cerr << "Warning: No serializer for the payload's type." << std::endl;
            }
        }
        return parts;
    }

private:
    std::map<Command, std::function<std::any(const std::string&)>> m_deserializers;
    std::map<std::type_index, std::function<std::string(const std::any&)>> m_serializers;
    std::map<Command, std::type_index> m_command_to_type;
};