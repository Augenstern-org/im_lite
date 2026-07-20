//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_MESSAGE_H
#define COM_LITE_MESSAGE_H

#include <string>

namespace types {
    // 消息基类
    struct MessageBase {
        std::string from_uid_;
        std::string to_uid_;
        std::string chat_type_;
        std::string client_msg_id_;
        std::string msg_type_;
    };

    struct RequestMsg : MessageBase {
        std::string content_;
    };

    struct ResponseMsg : MessageBase {
        std::string server_seq_;
    };
}

#endif //COM_LITE_MESSAGE_H