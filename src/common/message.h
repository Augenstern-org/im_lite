//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_MESSAGE_H
#define COM_LITE_MESSAGE_H

#define MAX_MESSAGE_PACK_BODY_LENGTH 65530

#include <string>
#include "json.hpp"

namespace types {
    enum class ChatTypes : uint8_t {
        single = 0,
        group  = 1
    };

    enum class MessageTypes : uint8_t {
        text = 0,
        pic  = 1
    };

    // 消息基类
    struct MessageBase {
        ChatTypes    chat_type_;
        MessageTypes msg_type_;
        std::string  from_uid_;
        std::string  to_uid_;
        std::string  client_msg_id_;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageBase, from_uid_, to_uid_, chat_type_, client_msg_id_, msg_type_)


    struct RequestMsg : MessageBase {
        std::string content_;
    };

    NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(RequestMsg, MessageBase, content_)


    struct ResponseMsg : MessageBase {
        std::string server_seq_;
    };

    NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(ResponseMsg, MessageBase, server_seq_)
}

#endif //COM_LITE_MESSAGE_H