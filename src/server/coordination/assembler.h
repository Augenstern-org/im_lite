//
// Created by Neuroil on 2026/7/29.
//

#ifndef COM_LITE_ASSEMBLER_H
#define COM_LITE_ASSEMBLER_H

#include <utility>
#include <vector>

#include "common/binary_code.h"
#include "common/io_status.h"
#include "common/message_pack.h"
namespace coordination {
    // 装配器：只负责把出站 MessagePack 装配出来，不决定它们送到哪个 fd。
    // 签名里没有 fd、也不持有 UsersGroup —— 路由（to_uid_ -> fd 集合）是独立的、
    // 尚未落地的职责。跨 fd 写入实现之前（core.cpp:183），
    // 产出指向其它 fd 的帧会导致误投；装配器无法表达 fd，该错误因此不可发生。Z}
    class Assembler {
    public:
        Assembler()  = default;
        ~Assembler() = default;

        // 把入站 request 的响应帧追加进 out。只追加，不清空 out。
        // 前置条件：in.is_valid() 为真（调用方保证）。
        // 返回 ok = 装配成功、已追加 0 或多条；error = 装配失败、out 一条未追加。
        //
        // status 由调用方定（ok = 已路由至目标 / fail = 目标不在线，无处投递），与
        // assemble_login_result 对称。有意不给默认值：默认 ok 等价于保留「无论路由
        // 结果如何都回 ok」的旧行为，而那正是本参数要修的缺陷。
        // 这里收的是 status 而非 fd —— 装配器仍然对 fd 无感知，路由不是它的职责（见类注释）。
        types::IoStatus assemble_response(const MessagePack& in, types::Status st, std::vector<MessagePack>& out) {
            MessagePack res = in;
            // 请求 / 响应由帧头 opcode 承载，不由类型区分（docs/architecture.md §2.5）。
            res.fh_.opcode_ = types::Opcode::response;
            res.fh_.status_ = st;
            // body_len_ 由 codec::Encoder::encode 回写，此处不预置。
            // 帧体当前是入站 Message 的原样复制 —— 真正该带的服务端序号见 TODO.md。
            out.push_back(std::move(res));
            return types::IoStatus::ok;
        }

        // 把入站 ack 的应答帧追加进 out。约定同上。
        types::IoStatus assemble_ack(const MessagePack& in, std::vector<MessagePack>& out) {
            MessagePack ack = in;
            // 占位应答体，与迁移前逐字节等价：content_ 是 std::string，走 operator=(char)，
            // 结果是长度 1、内容为字节 0x01 的字符串。不是任何约定的应答体，见 TODO.md。
            // 帧头有意保持入站原样（迁移前即如此），不强制改写 opcode / status。
            ack.msg_.content_ = '\x01';
            out.push_back(std::move(ack));
            return types::IoStatus::ok;
        }

        // 登录结果帧：对入站 login 帧的应答。status 由调用方定
        // （ok = 登录成功 / fail = 凭证错误或未知用户），帧体原样回显入站 Message
        // （客户端凭帧头 status 判断结果，凭帧体核对 uid）。
        types::IoStatus assemble_login_result(const MessagePack& in, types::Status st, std::vector<MessagePack>& out) {
            MessagePack res = in;
            res.fh_.opcode_ = types::Opcode::response;
            res.fh_.status_ = st;
            out.push_back(std::move(res));
            return types::IoStatus::ok;
        }
    };
} // coordination

#endif //COM_LITE_ASSEMBLER_H
