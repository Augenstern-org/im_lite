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

static MessagePack make_login(const std::string& uid, const std::string& credential) {
    Message msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = uid;
    msg.to_uid_        = uid; // 登录帧 to_uid_ 无业务语义，填自身保持 is_valid()
    msg.client_msg_id_ = "login-001";
    msg.content_       = credential;

    FrameHeader fh(types::Opcode::login, types::Status::ok);
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
    using coordination::RegisterResult;
    using coordination::UsersGroup;
    using types::IoStatus;
    using types::Opcode;
    using types::Status;

    // ── auth: 静态用户表校验 ─────────────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        const Controller::UserTable users = {{"alice", "alice123"}};
        Controller ctrl(asm_, ug, users);

        // 凭证正确
        CHECK(ctrl.auth("alice", "alice123"));
        // 凭证错误
        CHECK(!ctrl.auth("alice", "wrong"));
        // 未知用户
        CHECK(!ctrl.auth("nobody", "anything"));
    }

    // ── register_user → query 闭环 ───────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        CHECK(ctrl.register_user("alice", 42) == RegisterResult::Registered);

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

        CHECK(ctrl.register_user("bob", 7) == RegisterResult::Registered);
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
        CHECK(ctrl.register_user("receiver", 99) == RegisterResult::Registered);
        // 发送方 fd 需已登录（未登录连接只收登录帧），这里直接注册模拟已登录
        CHECK(ctrl.register_user("sender", 42) == RegisterResult::Registered);

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

    // ── process: request 但目标未注册 → 只产生 response(fail)，不转发 ─
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        // 发送方 fd 需已登录（未登录连接只收登录帧），这里直接注册模拟已登录
        CHECK(ctrl.register_user("sender", 42) == RegisterResult::Registered);
        MessagePack                            req = make_request("notfound");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, req, out_queue);

        // 只有 response 帧：没有转发入队，所以 [0] 必是回给发送方的响应
        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42);
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::response);
        // 目标不在线 —— 发送方必须拿到 fail，而不是「已送达」的假象
        CHECK(out_queue[0].first.fh_.status_ == Status::fail);
    }

    // ── process: ack → 产生 ack 应答 ─────────────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        // 发送方 fd 需已登录（未登录连接只收登录帧），这里直接注册模拟已登录
        CHECK(ctrl.register_user("sender", 42) == RegisterResult::Registered);
        MessagePack                            ack_in = make_ack();
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, ack_in, out_queue);

        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42);
        // ack 应答: content_ = 0x01
        CHECK(out_queue[0].first.msg_.content_.size() == 1);
        CHECK(static_cast<unsigned char>(out_queue[0].first.msg_.content_[0]) == 0x01);
    }

    // ── process: 已登录连接发 response opcode → 静默丢弃 ─
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug);

        CHECK(ctrl.register_user("sender", 42) == RegisterResult::Registered);

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

    // ── process: login 成功 → response(ok) + uid 绑定 ────
    {
        Assembler  asm_;
        UsersGroup ug;
        const Controller::UserTable users = {{"alice", "alice123"}};
        Controller ctrl(asm_, ug, users);

        MessagePack                            login = make_login("alice", "alice123");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, login, out_queue);

        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42);
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::response);
        CHECK(out_queue[0].first.fh_.status_ == Status::ok);

        // fd 已绑定：has_fd 通过，query 可查
        CHECK(ug.has_fd(42));
        int fd = -1;
        CHECK(ctrl.query("alice", fd));
        CHECK(fd == 42);
    }

    // ── process: login 失败（凭证错）→ response(fail)、不绑定 ──
    {
        Assembler  asm_;
        UsersGroup ug;
        const Controller::UserTable users = {{"alice", "alice123"}};
        Controller ctrl(asm_, ug, users);

        MessagePack                            login = make_login("alice", "wrong");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, login, out_queue);

        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::response);
        CHECK(out_queue[0].first.fh_.status_ == Status::fail);
        CHECK(!ug.has_fd(42)); // 未绑定
    }

    // ── process: 已绑定 fd 上改登另一个 uid → response(fail)，原绑定不变 ──
    {
        Assembler  asm_;
        UsersGroup ug;
        const Controller::UserTable users = {{"alice", "alice123"}, {"bob", "bob123"}};
        Controller ctrl(asm_, ug, users);

        MessagePack                              login_a = make_login("alice", "alice123");
        std::vector<std::pair<MessagePack, int>> out_queue;
        ctrl.process(42, login_a, out_queue);
        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].first.fh_.status_ == Status::ok);

        // 同一 fd 重复登录同一 uid：幂等成功，仍回 ok
        MessagePack again = make_login("alice", "alice123");
        out_queue.clear();
        ctrl.process(42, again, out_queue);
        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].first.fh_.status_ == Status::ok);

        // fd 42 已属于 alice：bob 凭证正确也不得改绑
        MessagePack login_b = make_login("bob", "bob123");
        out_queue.clear();
        ctrl.process(42, login_b, out_queue);

        CHECK(out_queue.size() == 1);
        CHECK(out_queue[0].second == 42); // 连接保留
        CHECK(out_queue[0].first.fh_.opcode_ == Opcode::response);
        CHECK(out_queue[0].first.fh_.status_ == Status::fail);

        // 原绑定完好，且没有产生 bob -> {42} 的幽灵条目
        int fd = -1;
        CHECK(!ctrl.query("bob", fd));
        CHECK(ctrl.query("alice", fd) && fd == 42);
        CHECK(ug.has_fd(42));
        CHECK(ug.invariants_ok());
    }

    // ── process: 未登录连接发 request → 拒绝 ──────────────
    {
        Assembler  asm_;
        UsersGroup ug;
        Controller ctrl(asm_, ug); // 空用户表，任何登录都失败

        MessagePack                            req = make_request("receiver");
        std::vector<std::pair<MessagePack, int>> out_queue;

        ctrl.process(42, req, out_queue);

        CHECK(out_queue.empty());
        CHECK(!ug.has_fd(42));
    }

    std::cout << "test_controller: PASSED\n";
    return 0;
}
