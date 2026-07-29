//
// Created by Neuroil on 2026/7/27.
//
// 断言类别 (b)：手工构造的黄金字节 -> decode -> 断言字段值。
//
// 本文件 **绝不调用 Encoder**。所有输入都来自 test_support.h 的 make_frame——
// 它用显式移位写大端长度，不碰 htonl。这样即使编码器与解码器共享同一个字节序 bug，
// 本文件也会红。decoder.h 会传递性地把 encoder.h 拉进来（不可避免），但没有任何用例调用它。
//

#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "server/core/decoder.h"

namespace {
    // ---------------------------------------------------------------------
    // 黄金向量（手工推导，独立于编码器）
    // json_len = 92 + Σ(四个字符串值的字节长度)；frame_len = 6 + json_len
    // 键序为字节字典序：chat_type_ < client_msg_id_ < content_ < from_uid_ < msg_type_ < to_uid_
    // ---------------------------------------------------------------------

    const std::string kJsonGv1 =
            R"({"chat_type_":0,"client_msg_id_":"hash","content_":"Hello World!","from_uid_":"Neuroil","msg_type_":0,"to_uid_":"Evil"})";

    const std::string kJsonGv3 =
            R"({"chat_type_":1,"client_msg_id_":"id-7","content_":"你好世界","from_uid_":"惠惠","msg_type_":0,"to_uid_":"大王"})";

    const std::string kJsonGv4 =
            R"({"chat_type_":0,"client_msg_id_":"","content_":"","from_uid_":"","msg_type_":0,"to_uid_":""})";

    std::string gv2_content() {
        std::string content;
        for (int i = 0; i < 15; ++i) content += "0123456789";
        CHECK(content.size() == 150);
        return content;
    }

    std::string gv2_json() {
        const std::string j =
                R"({"chat_type_":1,"client_msg_id_":"msg-0001","content_":")"
                + gv2_content()
                + R"(","from_uid_":"alice","msg_type_":1,"to_uid_":"bob"})";
        CHECK(j.size() == 258);
        return j;
    }

    void verify_golden_vector_sizes() {
        CHECK(kJsonGv1.size() == 119);
        CHECK(gv2_json().size() == 258);
        CHECK(kJsonGv3.size() == 120);
        CHECK(kJsonGv4.size() == 92);
    }

    // GV-1 解码后应得到的全部字段。
    void check_gv1_fields(const MessagePack& rmp) {
        CHECK(rmp.fh_.opcode_ == core::Opcode::request);
        CHECK(rmp.fh_.status_ == core::Status::ok);
        CHECK(rmp.fh_.body_len_ == 119);
        CHECK(rmp.msg_.chat_type_ == types::ChatTypes::single);
        CHECK(rmp.msg_.msg_type_ == types::MessageTypes::text);
        CHECK(rmp.msg_.from_uid_ == "Neuroil");
        CHECK(rmp.msg_.to_uid_ == "Evil");
        CHECK(rmp.msg_.client_msg_id_ == "hash");
        CHECK(rmp.msg_.content_ == "Hello World!");
    }

    // ---------------------------------------------------------------------
    // D1
    // ---------------------------------------------------------------------
    void decode_hand_authored_ascii() {
        const std::vector<std::byte> in = make_frame(0x01, 0x00, kJsonGv1);
        CHECK(in.size() == 125);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::ok);
        check_gv1_fields(rmp);
    }

    // ---------------------------------------------------------------------
    // D2 —— 多字节 body_len 的入站方向：小端读取会把 258 读成 0x02010000
    // ---------------------------------------------------------------------
    void decode_hand_authored_multibyte_len() {
        const std::string            kJsonGv2 = gv2_json();
        const std::vector<std::byte> in       = make_frame(0x02, 0x01, kJsonGv2);
        CHECK(in.size() == 264);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::ok);
        CHECK(rmp.fh_.opcode_ == core::Opcode::response);
        CHECK(rmp.fh_.status_ == core::Status::fail);
        CHECK(rmp.fh_.body_len_ == 258);
        CHECK(rmp.msg_.content_.size() == 150);
        CHECK(rmp.msg_.from_uid_ == "alice");
    }

    // ---------------------------------------------------------------------
    // D3
    // ---------------------------------------------------------------------
    void decode_hand_authored_utf8_cjk() {
        const std::vector<std::byte> in = make_frame(0x01, 0x00, kJsonGv3);
        CHECK(in.size() == 126);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::ok);
        CHECK(rmp.fh_.body_len_ == 120);
        CHECK(rmp.msg_.from_uid_ == "惠惠");
        CHECK(rmp.msg_.to_uid_ == "大王");
        CHECK(rmp.msg_.content_ == "你好世界");
        CHECK(rmp.msg_.content_.size() == 12);
        CHECK(rmp.msg_.from_uid_.size() == 6);
    }

    // ---------------------------------------------------------------------
    // D4 —— 缺陷 C 的回归测试：缓冲区大于帧长时必须仍能解码。
    //        这正是 encode 产出的形状（1024 字节缓冲里只有 125 字节有效）。
    // ---------------------------------------------------------------------
    void decode_tolerates_oversized_buffer() {
        std::vector<std::byte> in = make_frame(0x01, 0x00, kJsonGv1);
        in.resize(1024); // 尾部补 899 个零字节
        CHECK(in.size() == 1024);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::ok);
        check_gv1_fields(rmp);
    }

    // ---------------------------------------------------------------------
    // D5
    // ---------------------------------------------------------------------
    void decode_accepts_exactly_sized_buffer() {
        const std::vector<std::byte> in = make_frame(0x01, 0x00, kJsonGv1);
        CHECK(in.size() == 6 + 119);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::ok);
        check_gv1_fields(rmp);
    }

    // ---------------------------------------------------------------------
    // D6 —— 不足帧头长度
    // ---------------------------------------------------------------------
    void decode_rejects_short_buffer() {
        MessagePack rmp;

        const std::vector<std::byte> empty;
        CHECK(core::Decoder::decode(empty, rmp) == types::IoStatus::error);

        std::vector<std::byte> five;
        five.resize(5, std::byte{0x00});
        CHECK(core::Decoder::decode(five, rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D7 —— 头部声明 119，实际只带 118 字节载荷
    // ---------------------------------------------------------------------
    void decode_rejects_truncated_body() {
        const std::vector<std::byte> in = make_frame(0x01, 0x00, 119u, kJsonGv1.substr(0, 118));
        CHECK(in.size() == 124);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D8 —— 对端声明 4 GiB：必须在界限检查处返回，绝不用该长度驱动分配
    // ---------------------------------------------------------------------
    void decode_rejects_absurd_declared_len() {
        const std::vector<std::byte> in = make_frame(0x01, 0x00, 0xFFFFFFFFu, kJsonGv1);
        CHECK(in.size() == 125);

        MessagePack rmp;
        CHECK(core::Decoder::decode(in, rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D9 - D12 —— 枚举值域校验先于 static_cast
    // ---------------------------------------------------------------------
    void decode_rejects_unknown_opcode() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x07, 0x00, kJsonGv1), rmp) == types::IoStatus::error);
    }

    void decode_rejects_init_opcode() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0xFF, 0x00, kJsonGv1), rmp) == types::IoStatus::error);
    }

    void decode_rejects_unknown_status() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x05, kJsonGv1), rmp) == types::IoStatus::error);
    }

    void decode_rejects_init_status() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0xFF, kJsonGv1), rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D13 —— 全部合法组合都必须放行，且 opcode / status 不得互换
    // ---------------------------------------------------------------------
    void decode_accepts_all_known_opcodes() {
        const std::uint8_t ops[3] = {0x00, 0x01, 0x02};
        const std::uint8_t sts[2] = {0x00, 0x01};

        for (const std::uint8_t op : ops) {
            for (const std::uint8_t st : sts) {
                MessagePack rmp;
                CHECK(core::Decoder::decode(make_frame(op, st, kJsonGv1), rmp) == types::IoStatus::ok);
                CHECK(static_cast<std::uint8_t>(rmp.fh_.opcode_) == op);
                CHECK(static_cast<std::uint8_t>(rmp.fh_.status_) == st);
            }
        }
    }

    // ---------------------------------------------------------------------
    // D14 - D18 —— JSON 层拒绝路径
    // ---------------------------------------------------------------------
    void decode_rejects_malformed_json() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, "{not json"), rmp) == types::IoStatus::error);
    }

    // 缺陷 A 的原始触发点：from_json 里的 at() 会抛 out_of_range.403。
    // 修复前这个异常逸出 noexcept 边界，直接 terminate 掉进程。
    void decode_rejects_missing_key() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, "{}"), rmp) == types::IoStatus::error);
        // 能执行到这里本身就是断言：进程没有被 terminate。
    }

    void decode_rejects_wrong_typed_key() {
        // from_uid_ 是数字而非字符串 -> get<Message>() 抛 type_error.302。
        // 用双参 make_frame 重载，长度由 body.size() 得出，避免手工数字节。
        const std::string body =
                R"({"chat_type_":0,"client_msg_id_":"hash","content_":"hi","from_uid_":123,"msg_type_":0,"to_uid_":"Evil"})";

        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, body), rmp) == types::IoStatus::error);
    }

    // "[1,2,3]" 能解析成功且不是 discarded，但不是对象 —— 必须由 is_object() 拦下。
    void decode_rejects_non_object_body() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, "[1,2,3]"), rmp) == types::IoStatus::error);
    }

    void decode_rejects_null_body() {
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, "null"), rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D19
    // ---------------------------------------------------------------------
    void decode_rejects_empty_content() {
        // GV-4 四个字符串全空 -> Message::is_valid() 为 false -> error。
        //
        // 这是当前有意保留的策略：空聊天正文被当作协议错误处理。
        // 「业务校验 vs 协议校验」的归属仍是一个悬而未决的语义问题（空正文究竟该由
        // 解码器拒绝，还是放行给上层业务判断），但当前行为已确认为预期行为，
        // 本用例锁定它，改动策略时此处必须同步修改。
        MessagePack rmp;
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, kJsonGv4), rmp) == types::IoStatus::error);
    }

    // ---------------------------------------------------------------------
    // D20 —— 失败时出参逐位不变（提交点语义）
    // ---------------------------------------------------------------------
    void decode_leaves_out_param_untouched_on_error() {
        MessagePack rmp;
        rmp.fh_.opcode_         = core::Opcode::ack;
        rmp.fh_.status_         = core::Status::fail;
        rmp.fh_.body_len_       = 4242;
        rmp.msg_.chat_type_     = types::ChatTypes::group;
        rmp.msg_.msg_type_      = types::MessageTypes::pic;
        rmp.msg_.from_uid_      = "SENTINEL_FROM";
        rmp.msg_.to_uid_        = "SENTINEL_TO";
        rmp.msg_.client_msg_id_ = "SENTINEL_ID";
        rmp.msg_.content_       = "SENTINEL_CONTENT";

        const auto check_sentinels = [&rmp]() {
            CHECK(rmp.fh_.opcode_ == core::Opcode::ack);
            CHECK(rmp.fh_.status_ == core::Status::fail);
            CHECK(rmp.fh_.body_len_ == 4242);
            CHECK(rmp.msg_.chat_type_ == types::ChatTypes::group);
            CHECK(rmp.msg_.msg_type_ == types::MessageTypes::pic);
            CHECK(rmp.msg_.from_uid_ == "SENTINEL_FROM");
            CHECK(rmp.msg_.to_uid_ == "SENTINEL_TO");
            CHECK(rmp.msg_.client_msg_id_ == "SENTINEL_ID");
            CHECK(rmp.msg_.content_ == "SENTINEL_CONTENT");
        };

        // 缺键（JSON 反序列化中途失败）
        CHECK(core::Decoder::decode(make_frame(0x01, 0x00, "{}"), rmp) == types::IoStatus::error);
        check_sentinels();

        // 未知 opcode（帧头阶段失败）
        CHECK(core::Decoder::decode(make_frame(0x07, 0x00, kJsonGv1), rmp) == types::IoStatus::error);
        check_sentinels();

        // 载荷截断（长度界限阶段失败）
        CHECK(
            core::Decoder::decode(make_frame(0x01, 0x00, 119u, kJsonGv1.substr(0, 118)), rmp)
            == types::IoStatus::error
        );
        check_sentinels();
    }

    // ---------------------------------------------------------------------
    // D21 / D22 —— 编译期约束
    // ---------------------------------------------------------------------
    void decode_is_noexcept() {
        static_assert(
            noexcept(core::Decoder::decode(
                std::declval<const std::vector<std::byte>&>(),
                std::declval<MessagePack&>()
            )),
            "Decoder::decode must be noexcept"
        );
    }

    // D20 的「出参逐位不变」与 decode 的 noexcept 都建立在提交点的移动赋值不抛之上。
    void message_pack_move_assign_is_nothrow() {
        static_assert(
            std::is_nothrow_move_assignable_v<MessagePack>,
            "MessagePack move-assign must be noexcept"
        );
        static_assert(
            std::is_nothrow_move_assignable_v<Message>,
            "Message move-assign must be noexcept"
        );
    }
} // namespace

int main() {
    verify_golden_vector_sizes();

    decode_hand_authored_ascii();
    decode_hand_authored_multibyte_len();
    decode_hand_authored_utf8_cjk();
    decode_tolerates_oversized_buffer();
    decode_accepts_exactly_sized_buffer();
    decode_rejects_short_buffer();
    decode_rejects_truncated_body();
    decode_rejects_absurd_declared_len();
    decode_rejects_unknown_opcode();
    decode_rejects_init_opcode();
    decode_rejects_unknown_status();
    decode_rejects_init_status();
    decode_accepts_all_known_opcodes();
    decode_rejects_malformed_json();
    decode_rejects_missing_key();
    decode_rejects_wrong_typed_key();
    decode_rejects_non_object_body();
    decode_rejects_null_body();
    decode_rejects_empty_content();
    decode_leaves_out_param_untouched_on_error();
    decode_is_noexcept();
    message_pack_move_assign_is_nothrow();

    std::printf("test_decoder: PASSED\n");
    return 0;
}
