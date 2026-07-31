//
// Created by Neuroil on 2026/7/29.
//

#ifndef COM_LITE_ASSEMBLER_H
#define COM_LITE_ASSEMBLER_H

#include <vector>

#include "common/io_status.h"
#include "common/message_pack.h"

namespace coordination {
    // 装配器：只负责把出站 MessagePack 装配出来，不决定它们送到哪个 fd。
    // 签名里没有 fd、也不持有 UsersGroup —— 路由（to_uid_ → fd 集合）是独立的、
    // 尚未落地的职责。跨 fd 写入实现之前（core.cpp:183），
    // 产出指向其它 fd 的帧会导致误投；装配器无法表达 fd，该错误因此不可发生。
    class Assembler {
    public:
        Assembler()  = default;
        ~Assembler() = default;

        // 把入站 request 的响应帧追加进 out。只追加，不清空 out。
        // 前置条件：in.is_valid() 为真（调用方保证）。
        // 返回 ok = 装配成功、已追加 0 或多条；error = 装配失败、out 一条未追加。
        types::IoStatus assemble_response(const MessagePack& in, std::vector<MessagePack>& out);

        // 把入站 ack 的应答帧追加进 out。约定同上。
        types::IoStatus assemble_ack(const MessagePack& in, std::vector<MessagePack>& out);
    };
} // coordination

#endif //COM_LITE_ASSEMBLER_H
