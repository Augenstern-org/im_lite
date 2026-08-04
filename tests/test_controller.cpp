#include "server/controller/controller.h"

#include "test_support.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "common/binary_code.h"
#include "common/frame_header.h"
#include "common/io_status.h"
#include "common/message.h"
#include "common/message_pack.h"

// ── 测试用 MessagePack 工厂 ──────────────────────────────

static MessagePack make_request(const std::string& to_uid, const std::string& content = "hello") {
    Message msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = "sender";
    msg.to_uid_        = to_uid;
    msg.client_msg_id_  = "req-001";
    msg.content_       = content;

    FrameHeader fh(types::Opcode::request, types::Status::ok);
    return MessagePack(fh, std::move(msg));
}

static MessagePack make_ack() {
    Message msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = "sender";
    msg.to_uid_        = "receiver";
    msg.client_msg_id_  = "ack-001";
    msg.content_       = "ack-data";

    FrameHeader fh(types::Opcode::ack, types::Status::ok);
    return MessagePack(fh, std::move(msg));
}

static MessagePack make_response_msg() {
    Message msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = "sender";
    msg.to_uid_        = "receiver";
    msg.client_msg_id_  = "res-001";
    msg.content_       = "data";

    FrameHeader fh(types::Opcode::response, types::Status::ok);
    return MessagePack(fh, std::move(msg));
}

int main() {
    using controller::Controller;
    using coordination::Assembler;
    using coordination::UsersGroup;
    using types::IoStatus;
    using types::Opcode;
    using types::Status;

    // ── auth: 目前总是返回 true ──────────────────────────
    {
        CHECK(Controller::auth("anyone"));
        CHECK(Controller::auth(""));
    }

    // ── register_user → query 闭环 ───────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        CHECK(ctrl.register_user("alice", 42));

        int fd = -1;
        CHECK(ctrl.query("alice", fd));
        CHECK(fd == 42);
    }

    // ── query 不存在的 uid → 返回 false ──────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        int fd = -1;
        CHECK(!ctrl.query("nobody", fd));
    }

    // ── delete_fd 通过 Controller ────────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        CHECK(ctrl.register_user("bob", 7));
        CHECK(ctrl.delete_fd(7));

        int fd = -1;
        CHECK(!ctrl.query("bob", fd));
    }

    // ── process: request → 产生 response + 转发帧 ────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        // 先注册目标用户，否则转发不会触发
        CHECK(ctrl.register_user("receiver", 99));

        MessagePack                            req = make_request("receiver");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, req, out_queue);

        // out_queue 应有两条：
        // [0] = 转发帧 → fd=99（目标用户）
        // [1] = response 帧 → fd=42（入站 fd）
        CHECK(out_queue.size() == 2);

        // 第一条：转发原始请求到 receiver 的 fd
        CHECK(out_queue[0].second == 99);
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::request);

        // 第二条：response 回到入站 fd
        CHECK(out_queue[1].second == 42);
        CHECK(out_queue[1].first.fh_.opcode_ == Opcode::response);
        CHECK(out_queue[1].first.fh_.status_ == Status::ok);
    }

    // ── process: request 但目标未注册 → 只产生 response，不转发 ─
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        MessagePack                            req = make_request("notfound");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, req, out_queue);

        // 只有 response 帧
        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42);
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::response);
    }

    // ── process: ack → 产生 ack 应答 ─────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        MessagePack                            ack_in = make_ack();
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, ack_in, out_queue);

        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42);
        // ack 应答: content_ = 0x01
        CHECK(out_queue[0].first.msg_.content_.size() == 1);
        CHECK(static_cast<unsigned char>(out_queue[0].first.msg_.content_[0]) == 0x01);
    }

    // ── process: response opcode → 静默丢弃 ──────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        MessagePack                            res = make_response_msg();
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, res, out_queue);

        CHECK(out_queue.empty());
    }

    // ── process: 无效消息 → 静默返回 ─────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        MessagePack invalid; // 默认构造，is_valid() == false
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, invalid, out_queue);

        CHECK(out_queue.empty());
    }

    std::cout << "test_controller: PASSED\n";
    return 0;
}
