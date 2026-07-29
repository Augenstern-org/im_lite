//
// Created by Neuroil on 2026/7/27.
//
// 断言类别 (c)：encode -> decode 往返。
//
// 这里的用例**只能**证明 encode 与 decode 彼此一致，不能证明任何一方在线格式上是对的：
// htonl 与 ntohl 互为逆运算，两端若共享同一个字节序错误，往返测试照样全绿。
// 字节层面的真值由 test_encoder.cpp 的 E1-E3（出站黄金字节）与
// test_decoder.cpp 的 D1-D3（手工构造的入站黄金字节）分别钉死。
// 本文件负责的是可组合性：完整链路上字段不丢、不错位、不被 1024 字节缓冲区的尾部零字节干扰。
//

#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "server/core/decoder.h"
#include "server/core/encoder.h"

namespace {
    Message msg_gv1() {
        Message m;
        m.chat_type_     = types::ChatTypes::single;
        m.msg_type_      = types::MessageTypes::text;
        m.from_uid_      = "Neuroil";
        m.to_uid_        = "Evil";
        m.client_msg_id_ = "hash";
        m.content_       = "Hello World!";
        return m;
    }

    Message msg_gv2() {
        std::string content;
        for (int i = 0; i < 15; ++i) content += "0123456789";
        CHECK(content.size() == 150);

        Message m;
        m.chat_type_     = types::ChatTypes::group;
        m.msg_type_      = types::MessageTypes::pic;
        m.from_uid_      = "alice";
        m.to_uid_        = "bob";
        m.client_msg_id_ = "msg-0001";
        m.content_       = content;
        return m;
    }

    Message msg_gv3() {
        Message m;
        m.chat_type_     = types::ChatTypes::group;
        m.msg_type_      = types::MessageTypes::text;
        m.from_uid_      = "惠惠";
        m.to_uid_        = "大王";
        m.client_msg_id_ = "id-7";
        m.content_       = "你好世界";
        return m;
    }

    void check_fields_equal(const Message& a, const Message& b) {
        CHECK(a.chat_type_ == b.chat_type_);
        CHECK(a.msg_type_ == b.msg_type_);
        CHECK(a.from_uid_ == b.from_uid_);
        CHECK(a.to_uid_ == b.to_uid_);
        CHECK(a.client_msg_id_ == b.client_msg_id_);
        CHECK(a.content_ == b.content_);
    }

    // ---------------------------------------------------------------------
    // R1 —— 整个未裁剪的 1024 字节缓冲区直接喂给 decode。
    //        修复前 decode 在这里返回 error（缓冲区尺寸被当成帧长）。
    // ---------------------------------------------------------------------
    void roundtrip_ascii() {
        const Message  original = msg_gv1();
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        MessagePack src(fh, original);

        std::vector<std::byte> buf;
        buf.resize(1024, std::byte{0x00});
        std::uint32_t out_len = 0;

        CHECK(core::Encoder::encode(src, buf, out_len) == types::IoStatus::ok);
        CHECK(out_len == 125);
        CHECK(buf.size() == 1024);   // 不裁剪

        MessagePack dst;
        CHECK(core::Decoder::decode(buf, dst) == types::IoStatus::ok);

        check_fields_equal(original, dst.msg_);
        CHECK(dst.fh_.body_len_ == 119);
        CHECK(dst.fh_.opcode_ == core::Opcode::request);
        CHECK(dst.fh_.status_ == core::Status::ok);
    }

    // ---------------------------------------------------------------------
    // R2
    // ---------------------------------------------------------------------
    void roundtrip_multibyte_len() {
        const Message  original = msg_gv2();
        FrameHeader              fh(core::Opcode::response, core::Status::fail);
        MessagePack src(fh, original);

        std::vector<std::byte> buf;
        buf.resize(1024, std::byte{0x00});
        std::uint32_t out_len = 0;

        CHECK(core::Encoder::encode(src, buf, out_len) == types::IoStatus::ok);
        CHECK(out_len == 264);

        MessagePack dst;
        CHECK(core::Decoder::decode(buf, dst) == types::IoStatus::ok);

        check_fields_equal(original, dst.msg_);
        CHECK(dst.fh_.body_len_ == 258);
        CHECK(dst.msg_.content_.size() == 150);
        CHECK(dst.fh_.opcode_ == core::Opcode::response);
        CHECK(dst.fh_.status_ == core::Status::fail);
    }

    // ---------------------------------------------------------------------
    // R3
    // ---------------------------------------------------------------------
    void roundtrip_cjk() {
        const Message  original = msg_gv3();
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        MessagePack src(fh, original);

        std::vector<std::byte> buf;
        buf.resize(1024, std::byte{0x00});
        std::uint32_t out_len = 0;

        CHECK(core::Encoder::encode(src, buf, out_len) == types::IoStatus::ok);
        CHECK(out_len == 126);

        MessagePack dst;
        CHECK(core::Decoder::decode(buf, dst) == types::IoStatus::ok);

        check_fields_equal(original, dst.msg_);
        CHECK(dst.msg_.from_uid_ == "惠惠");
        CHECK(dst.msg_.content_ == "你好世界");
        CHECK(dst.msg_.from_uid_.size() == 6);
        CHECK(dst.msg_.content_.size() == 12);
        CHECK(dst.fh_.body_len_ == 120);
    }

    // ---------------------------------------------------------------------
    // R4 —— opcode 与 status 都是 1 字节相邻字段，取值刻意不等以暴露互换
    // ---------------------------------------------------------------------
    void roundtrip_no_field_transposition() {
        struct Case {
            core::Opcode op;
            core::Status st;
        };
        const Case cases[2] = {
            {core::Opcode::ack, core::Status::fail},        // 0x00 / 0x01
            {core::Opcode::response, core::Status::ok}      // 0x02 / 0x00
        };

        for (const Case& c : cases) {
            FrameHeader              fh(c.op, c.st);
            MessagePack src(fh, msg_gv1());

            std::vector<std::byte> buf;
            buf.resize(1024, std::byte{0x00});
            std::uint32_t out_len = 0;

            CHECK(core::Encoder::encode(src, buf, out_len) == types::IoStatus::ok);

            MessagePack dst;
            CHECK(core::Decoder::decode(buf, dst) == types::IoStatus::ok);
            CHECK(dst.fh_.opcode_ == c.op);
            CHECK(dst.fh_.status_ == c.st);
        }
    }

    // ---------------------------------------------------------------------
    // R5 —— 裁剪到 out_len 的精确尺寸缓冲区同样成立
    // ---------------------------------------------------------------------
    void roundtrip_trimmed_buffer() {
        const Message  original = msg_gv1();
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        MessagePack src(fh, original);

        std::vector<std::byte> buf;
        buf.resize(1024, std::byte{0x00});
        std::uint32_t out_len = 0;

        CHECK(core::Encoder::encode(src, buf, out_len) == types::IoStatus::ok);
        buf.resize(out_len);
        CHECK(buf.size() == 125);

        MessagePack dst;
        CHECK(core::Decoder::decode(buf, dst) == types::IoStatus::ok);

        check_fields_equal(original, dst.msg_);
        CHECK(dst.fh_.body_len_ == 119);
    }
} // namespace

int main() {
    roundtrip_ascii();
    roundtrip_multibyte_len();
    roundtrip_cjk();
    roundtrip_no_field_transposition();
    roundtrip_trimmed_buffer();

    std::printf("test_roundtrip: PASSED\n");
    return 0;
}
