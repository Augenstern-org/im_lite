//
// Created by Neuroil on 2026/7/16.
//

#include <iostream>
#include <stdexcept>
#include "server/controller/controller.h"

namespace controller {
    // Controller::AckAssembler Controller::ack_assembler_ = nullptr;
    Controller::ResAssembler Controller::res_assembler_ = nullptr;


    void Controller::process(int fd, const MessagePack& rmp, std::vector<std::pair<MessagePack, int>>& out_queue) {
        if (!res_assembler_) throw std::runtime_error("assemblers have no valid objs!");

        // Opcode 判断消息类型
        // req -> 转发req + 返回res
        // ack -> 应答ack
        // 只有服务端才会返回res
        if (rmp.fh_.opcode_ == types::Opcode::ack) {
            MessagePack ack   = rmp;
            ack.msg_.content_ = 0x01;
            out_queue.push_back({ack, fd});
        } else if (rmp.fh_.opcode_ == types::Opcode::request) {
            out_queue.push_back({rmp, fd});

            // 解析当前消息（其实from_user_id_字段就够了）
            // UserGroup::search(int fd) -> User u

            // 解析需要送到哪个fd（也可以合为一个函数）
            // 或者说直接解析到 Connection 可能更好
            // UserGroup::search(std::string user_id) -> User u
            // UserGroup::get_user_fd(User user) -> int target_fd

            // 入队
            // out_queue.push_back({msg_pack, target_fd})
        }
    }
} // controller
