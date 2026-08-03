//
// Created by Neuroil on 2026/7/16.
//

#include "server/controller/controller.h"

#include <utility>
#include <vector>

#include "common/binary_code.h"
#include "common/io_status.h"

namespace controller {
    void Controller::process(
        int                                       fd,
        const MessagePack&                        rmp,
        std::vector<std::pair<MessagePack, int>>& out_queue
    ) {
        // 入口校验（docs/architecture.md §2.1）。message_handler_ 只在 decode() 返回 ok 时被调用
        // （core.cpp drain 循环），而 ok 蕴含 msg.is_valid() 已过且帧头两枚举已知 —— 本检查是
        // 文档化的死代码：Controller::process 是公有方法，跨层防御便宜，兜底保留
        // （docs/logs/2026-07-31.md §4.6）。
        if (!rmp.is_valid()) {
            return;
        }

        std::vector<MessagePack> assembled;
        types::IoStatus          st = types::IoStatus::ok;

        // Opcode 判断消息类型：req -> 返回 res（转发待跨 fd 写入落地）；ack -> 应答 ack。
        if (rmp.fh_.opcode_ == types::Opcode::ack) {
            st = assembler_.assemble_ack(rmp, assembled);
        } else if (rmp.fh_.opcode_ == types::Opcode::request) {
            // 回复消息
            st = assembler_.assemble_response(rmp, assembled);

            // 查询 fd
            bool success = true;
            int  to_fd   = -1;

            std::string uid = rmp.msg_.to_uid_;

            success = query(uid, to_fd);
            // 转发
            if (success) out_queue.emplace_back(rmp, to_fd);
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

    bool Controller::query(const std::string& uid, int& fd) const {
        return users_group_.query(uid, fd);
    }
} // controller
