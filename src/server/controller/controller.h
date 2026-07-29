//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_CONTROLLER_H
#define COM_LITE_CONTROLLER_H

#include <iostream>
#include <vector>

#include "server/coordination/assembler.h"
#include "server/coordination/users_group.h"
#include "common/message_pack.h"

namespace controller {
    class Controller {
        static void request(int fd, const Message& msg, std::vector<MessagePack>& out_queue) {
            //
        }

        static void ack(int fd, std::vector<MessagePack>& out_queue) {
            // 直接返回应答
        }

        coordination::Assembler ack_assembler_;
        coordination::Assembler res_assembler_;

    public:
        Controller() = default;

        static void process(int fd, const MessagePack& rmp, std::vector<MessagePack>& out_queue) {
            std::cout << "uid: " << rmp.msg_.from_uid_ << "\n"
                    << "content: " << rmp.msg_.content_ << std::endl;

            // Opcode 判断消息类型
            // req -> 转发req + 返回res
            // ack -> 应答ack
            // 只有服务端才会返回res

            // 所有消息压入消息队列，供 encoder 消费
            // 目前仍是直接回显
            out_queue.push_back(rmp);
        }
    };
} // controller

#endif //COM_LITE_CONTROLLER_H
