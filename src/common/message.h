//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_MESSAGE_H
#define COM_LITE_MESSAGE_H

#include <string>
#include "json.hpp"

static constexpr std::size_t max_message_body_length = 65530;

namespace types {
    enum class ChatTypes : uint8_t {
        _init_ = 0xff,
        single = 0,
        group  = 1
    };

    enum class MessageTypes : uint8_t {
        _init_ = 0xff,
        text = 0,
        pic  = 1
    };

    // 消息基类
    struct MessageBase {
        ChatTypes    chat_type_ = ChatTypes::_init_;
        MessageTypes msg_type_ = MessageTypes::_init_;
        std::string  from_uid_;
        std::string  to_uid_;
        std::string  client_msg_id_;

        bool is_init() const {
            return chat_type_ != ChatTypes::_init_
                    && msg_type_ != MessageTypes::_init_
                    && !from_uid_.empty()
                    && !to_uid_.empty()
                    && !client_msg_id_.empty();
        }
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageBase, from_uid_, to_uid_, chat_type_, client_msg_id_, msg_type_)


    struct RequestMsg : MessageBase {
        std::string content_;

        bool is_valid() {
            return is_init() && !content_.empty();
        }
    };

    NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(RequestMsg, MessageBase, content_)


    struct ResponseMsg : MessageBase {
        std::string server_seq_;

        bool is_valid() {
            return is_init() && !server_seq_.empty();
        }
    };

    NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(ResponseMsg, MessageBase, server_seq_)
}

#endif //COM_LITE_MESSAGE_H
