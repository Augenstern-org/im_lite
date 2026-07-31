//
// Created by Neuroil on 2026/7/30.
//

#include "server/coordination/assembler.h"

#include <utility>

#include "common/binary_code.h"

namespace coordination {
    types::IoStatus Assembler::assemble_response(const MessagePack& in, std::vector<MessagePack>& out) {
        MessagePack res = in;
        // 请求 / 响应由帧头 opcode 承载，不由类型区分（docs/architecture.md §2.5）。
        res.fh_.opcode_ = types::Opcode::response;
        res.fh_.status_ = types::Status::ok;
        // body_len_ 由 codec::Encoder::encode 回写，此处不预置。
        // 帧体当前是入站 Message 的原样复制 —— 真正该带的服务端序号见 TODO.md。
        out.push_back(std::move(res));
        return types::IoStatus::ok;
    }

    types::IoStatus Assembler::assemble_ack(const MessagePack& in, std::vector<MessagePack>& out) {
        MessagePack ack = in;
        // 占位应答体，与迁移前逐字节等价：content_ 是 std::string，走 operator=(char)，
        // 结果是长度 1、内容为字节 0x01 的字符串。不是任何约定的应答体，见 TODO.md。
        // 帧头有意保持入站原样（迁移前即如此），不强制改写 opcode / status。
        ack.msg_.content_ = '\x01';
        out.push_back(std::move(ack));
        return types::IoStatus::ok;
    }
} // coordination
