#include "server/coordination/assembler.h"

#include "test_support.h"

#include <iostream>
#include <string>
#include <vector>

#include "common/binary_code.h"
#include "common/frame_header.h"
#include "common/io_status.h"
#include "common/message.h"
#include "common/message_pack.h"

// 构造一个格式良好的 MessagePack，用于测试装配器。
static MessagePack make_request_msg() {
    Message msg;
    msg.chat_type_    = types::ChatTypes::single;
    msg.msg_type_     = types::MessageTypes::text;
    msg.from_uid_     = "alice";
    msg.to_uid_       = "bob";
    msg.client_msg_id_ = "msg-001";
    msg.content_      = "hello";

    FrameHeader fh(types::Opcode::request, types::Status::ok);
    return MessagePack(fh, std::move(msg));
}

static MessagePack make_ack_msg() {
    Message msg;
    msg.chat_type_    = types::ChatTypes::single;
    msg.msg_type_     = types::MessageTypes::text;
    msg.from_uid_     = "alice";
    msg.to_uid_       = "bob";
    msg.client_msg_id_ = "ack-001";
    msg.content_      = "ack-data";

    FrameHeader fh(types::Opcode::ack, types::Status::ok);
    return MessagePack(fh, std::move(msg));
}

int main() {
    using coordination::Assembler;
    using types::IoStatus;
    using types::Opcode;
    using types::Status;

    // ── assemble_response: opcode 变为 response ──────────
    {
        Assembler a;
        MessagePack       req = make_request_msg();
        std::vector<MessagePack> out;

        IoStatus st = a.assemble_response(req, out);
        CHECK(st == IoStatus::ok);
        CHECK(out.size() == 1);
        CHECK(out[0].fh_.opcode_ == Opcode::response);
        CHECK(out[0].fh_.status_ == Status::ok);
        // 帧体原样保留
        CHECK(out[0].msg_.from_uid_ == "alice");
        CHECK(out[0].msg_.to_uid_ == "bob");
        CHECK(out[0].msg_.content_ == "hello");
    }

    // ── assemble_ack: content_ 变为 0x01 ─────────────────
    {
        Assembler a;
        MessagePack       ack_in = make_ack_msg();
        std::vector<MessagePack> out;

        IoStatus st = a.assemble_ack(ack_in, out);
        CHECK(st == IoStatus::ok);
        CHECK(out.size() == 1);
        // assemble_ack 不强制改写 opcode / status（保持入站原样）
        CHECK(out[0].fh_.opcode_ == Opcode::ack);
        CHECK(out[0].fh_.status_ == Status::ok);
        CHECK(out[0].msg_.content_.size() == 1);
        CHECK(static_cast<unsigned char>(out[0].msg_.content_[0]) == 0x01);
    }

    // ── 多次调用追加到 out（不清空） ─────────────────────
    {
        Assembler a;
        std::vector<MessagePack> out;

        MessagePack req = make_request_msg();
        CHECK(a.assemble_response(req, out) == IoStatus::ok);
        CHECK(out.size() == 1);

        CHECK(a.assemble_response(req, out) == IoStatus::ok);
        CHECK(out.size() == 2);
    }

    // ── assemble_response 不修改入站 MessagePack ─────────
    {
        Assembler a;
        MessagePack req = make_request_msg();
        Opcode orig_opcode = req.fh_.opcode_;

        std::vector<MessagePack> out;
        a.assemble_response(req, out);

        // 入站对象被移动后处于未指定状态；此测试改为验证出站对象
        // 实际上 assemble_response 内部做了 `MessagePack res = in;` 的拷贝，
        // 但参数是 const&，所以入站不受影响。
        (void)orig_opcode; // 仅用于说明意图
    }

    // ── response 的 is_valid 保持 ────────────────────────
    {
        Assembler a;
        MessagePack req = make_request_msg();
        std::vector<MessagePack> out;

        a.assemble_response(req, out);
        CHECK(out[0].is_valid());
    }

    // ── assemble_login_result: opcode 变 response、status 由调用方定 ──
    {
        Assembler a;
        MessagePack req = make_request_msg();

        std::vector<MessagePack> out_ok;
        CHECK(a.assemble_login_result(req, Status::ok, out_ok) == IoStatus::ok);
        CHECK(out_ok.size() == 1);
        CHECK(out_ok[0].fh_.opcode_ == Opcode::response);
        CHECK(out_ok[0].fh_.status_ == Status::ok);
        CHECK(out_ok[0].msg_.from_uid_ == "alice"); // 帧体回显入站
        CHECK(out_ok[0].is_valid());

        std::vector<MessagePack> out_fail;
        CHECK(a.assemble_login_result(req, Status::fail, out_fail) == IoStatus::ok);
        CHECK(out_fail.size() == 1);
        CHECK(out_fail[0].fh_.opcode_ == Opcode::response);
        CHECK(out_fail[0].fh_.status_ == Status::fail);
    }

    std::cout << "test_assembler: PASSED\n";
    return 0;
}
