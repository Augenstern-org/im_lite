//
// Created by Neuroil on 2026/7/16.
//

#include <iostream>
#include <stdexcept>
#include "server/controller/controller.h"

namespace controller {
    Controller::AckAssembler Controller::ack_assembler_ = nullptr;
    Controller::ResAssembler Controller::res_assembler_ = nullptr;


    void Controller::process(int fd, const MessagePack& rmp, std::vector<MessagePack>& out_queue) {
        std::cout << "uid: " << rmp.msg_.from_uid_ << "\n"
                << "content: " << rmp.msg_.content_ << std::endl;

        // Opcode 判断消息类型
        // req -> 转发req + 返回res
        // ack -> 应答ack
        // 只有服务端才会返回res
        if (!ack_assembler_ || !res_assembler_) throw std::runtime_error("assemblers have no valid objs!");



        // 所有消息压入消息队列，供 encoder 消费
        // 目前仍是直接回显
        out_queue.push_back(rmp);
    }
} // controller
