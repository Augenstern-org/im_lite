//
// Created by Neuroil on 2026/7/16.
//

#include "server/controller/controller.h"

#include <utility>
#include <vector>

#include "common/binary_code.h"
#include "common/io_status.h"

namespace controller {
    void Controller::process(int fd, const MessagePack& rmp,
                             std::vector<std::pair<MessagePack, int>>& out_queue) {
        // 入口校验（docs/architecture.md §2.1）。core.cpp:157 暂未使用 decode 的返回状态，
        // 解码失败时一个默认构造的空 MessagePack 会到达这里。
        if (!rmp.is_valid()) {
            return;
        }

        std::vector<MessagePack> assembled;
        types::IoStatus          st = types::IoStatus::ok;

        // Opcode 判断消息类型：req -> 返回 res（转发待跨 fd 写入落地）；ack -> 应答 ack。
        if (rmp.fh_.opcode_ == types::Opcode::ack) {
            st = assembler_.assemble_ack(rmp, assembled);
        } else if (rmp.fh_.opcode_ == types::Opcode::request) {
            st = assembler_.assemble_response(rmp, assembled);
            // 转发给 to_uid_ 的那一帧尚未装配 —— 跨 fd 写入未实现（core.cpp:175）。
            // 现在产出外部 fd 会让帧被投错人
        } else {
            // 只有服务端才会返回 res。
            // 因此不接收 response，当前静默丢弃
            return;
        }

        if (st != types::IoStatus::ok) {
            return;
        }

        // 目前所有出站帧的目标 fd 恒等于入站 fd。
        for (MessagePack& mp : assembled) {
            out_queue.emplace_back(std::move(mp), fd);
        }
    }
} // controller
