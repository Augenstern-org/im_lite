//
// Created by Neuroil on 2026/7/21.
//
// 断言类别 (a)：encode 的输出与手工推导的黄金字节逐字节比对。
// 这里只钉「出站线格式」——不调用 decode，因此 htonl/ntohl 若同时写错也无法互相掩盖。
//

#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/encoder.h"

namespace {
    // ---------------------------------------------------------------------
    // 黄金向量（手工推导）
    //
    // 长度公式：json_len = 92 + Σ(四个字符串值的字节长度)
    //   —— 前提是两个枚举均为个位数、且无 JSON 转义。
    //   92 = 2 花括号 + 5 逗号 + 6 冒号 + 键名 57 + 键名引号 12 + 枚举值 2 + 字符串值引号 8
    // 帧长：frame_len = 6 + json_len
    //
    // 键序为字节字典序（nlohmann::json 默认 std::map 底层）：
    //   chat_type_ < client_msg_id_ < content_ < from_uid_ < msg_type_ < to_uid_
    // 注意 content_ 虽是派生类字段，仍按字典序排在中间。
    // ---------------------------------------------------------------------

    const std::string kJsonGv1 =
        R"({"chat_type_":0,"client_msg_id_":"hash","content_":"Hello World!","from_uid_":"Neuroil","msg_type_":0,"to_uid_":"Evil"})";

    const std::string kJsonGv3 =
        R"({"chat_type_":1,"client_msg_id_":"id-7","content_":"你好世界","from_uid_":"惠惠","msg_type_":0,"to_uid_":"大王"})";

    const std::string kJsonGv4 =
        R"({"chat_type_":0,"client_msg_id_":"","content_":"","from_uid_":"","msg_type_":0,"to_uid_":""})";

    // GV-2 的 content_ 为 "0123456789" 重复 15 次 = 150 字节，程序化拼出以免手抄出错。
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

    // 黄金向量常量一律先验长度：字面量抄错必须当场炸掉，而不是悄悄把后续字节断言挪位。
    void verify_golden_vector_sizes() {
        CHECK(kJsonGv1.size() == 119);
        CHECK(gv2_json().size() == 258);
        CHECK(kJsonGv3.size() == 120);
        CHECK(kJsonGv4.size() == 92);
    }

    // ---------------------------------------------------------------------
    // 消息构造
    // ---------------------------------------------------------------------

    types::RequestMsg msg_gv1() {
        types::RequestMsg m;
        m.chat_type_     = types::ChatTypes::single;
        m.msg_type_      = types::MessageTypes::text;
        m.from_uid_      = "Neuroil";
        m.to_uid_        = "Evil";
        m.client_msg_id_ = "hash";
        m.content_       = "Hello World!";
        return m;
    }

    types::RequestMsg msg_gv2() {
        types::RequestMsg m;
        m.chat_type_     = types::ChatTypes::group;
        m.msg_type_      = types::MessageTypes::pic;
        m.from_uid_      = "alice";
        m.to_uid_        = "bob";
        m.client_msg_id_ = "msg-0001";
        m.content_       = gv2_content();
        return m;
    }

    types::RequestMsg msg_gv3() {
        types::RequestMsg m;
        m.chat_type_     = types::ChatTypes::group;
        m.msg_type_      = types::MessageTypes::text;
        m.from_uid_      = "惠惠";
        m.to_uid_        = "大王";
        m.client_msg_id_ = "id-7";
        m.content_       = "你好世界";
        return m;
    }

    types::RequestMsg msg_gv4() {
        types::RequestMsg m;
        m.chat_type_     = types::ChatTypes::single;
        m.msg_type_      = types::MessageTypes::text;
        m.from_uid_      = "";
        m.to_uid_        = "";
        m.client_msg_id_ = "";
        m.content_       = "";
        return m;
    }

    std::vector<std::byte> fresh_buf(std::size_t n, std::byte fill) {
        std::vector<std::byte> b;
        b.resize(n, fill);
        return b;
    }

    // ---------------------------------------------------------------------
    // E1
    // ---------------------------------------------------------------------
    void encode_golden_ascii() {
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        core::RequestMessagePack rmp(fh, msg_gv1());

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0x00});
        std::uint32_t          out_len = 0;

        const types::IoStatus st = core::Encoder::encode(rmp, buf, out_len);
        CHECK(st == types::IoStatus::ok);
        CHECK(out_len == 125);

        // 帧头逐字节钉死。绝不 reinterpret_cast 回 uint32_t 再比 —— 那在小端上是重言式，
        // 且违反 SEI CERT POS39-C / 严格别名。
        CHECK(buf[0] == std::byte{0x01});   // opcode = request
        CHECK(buf[1] == std::byte{0x00});   // status = ok
        CHECK(buf[2] == std::byte{0x00});
        CHECK(buf[3] == std::byte{0x00});
        CHECK(buf[4] == std::byte{0x00});
        CHECK(buf[5] == std::byte{0x77});   // body_len = 119，大端

        // 这一条等式同时钉住了键序（字节字典序）与序列化参数。
        CHECK(body_of(buf, 6, 119) == kJsonGv1);

        // 整帧与独立构造的参考帧比对（make_frame 不经过 htonl，字节序独立）。
        const std::vector<std::byte> expect = make_frame(0x01, 0x00, kJsonGv1);
        CHECK(expect.size() == 125);
        for (std::size_t i = 0; i < 125; ++i) {
            CHECK(buf[i] == expect[i]);
        }
    }

    // ---------------------------------------------------------------------
    // E2 —— 字节序判别测试：小端 bug 会写出 02 01 00 00 而不是 00 00 01 02
    // ---------------------------------------------------------------------
    void encode_golden_multibyte_len() {
        const std::string kJsonGv2 = gv2_json();

        FrameHeader              fh(core::Opcode::response, core::Status::fail);
        core::RequestMessagePack rmp(fh, msg_gv2());

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0x00});
        std::uint32_t          out_len = 0;

        const types::IoStatus st = core::Encoder::encode(rmp, buf, out_len);
        CHECK(st == types::IoStatus::ok);
        CHECK(out_len == 264);

        CHECK(buf[0] == std::byte{0x02});   // opcode = response
        CHECK(buf[1] == std::byte{0x01});   // status = fail，与 opcode 不同值，防止字段错位被掩盖
        CHECK(buf[2] == std::byte{0x00});
        CHECK(buf[3] == std::byte{0x00});
        CHECK(buf[4] == std::byte{0x01});
        CHECK(buf[5] == std::byte{0x02});   // 258 = 0x00000102

        CHECK(body_of(buf, 6, 258) == kJsonGv2);
    }

    // ---------------------------------------------------------------------
    // E3
    // ---------------------------------------------------------------------
    void encode_golden_utf8_cjk() {
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        core::RequestMessagePack rmp(fh, msg_gv3());

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0x00});
        std::uint32_t          out_len = 0;

        const types::IoStatus st = core::Encoder::encode(rmp, buf, out_len);
        CHECK(st == types::IoStatus::ok);
        CHECK(out_len == 126);
        CHECK(buf[5] == std::byte{0x78});   // body_len = 120

        const std::string body = body_of(buf, 6, 120);
        CHECK(body == kJsonGv3);

        // ensure_ascii=false：CJK 必须原样落字节，不得出现 \uXXXX 转义。
        // 转义会把 12 字节的 "你好世界" 变成 24 字节，长度公式随之失效。
        CHECK(body.find('\\') == std::string::npos);

        // "你" 的 UTF-8 编码 E4 BD A0 —— 定位到 content_ 值的首字节再比对。
        const std::size_t marker = kJsonGv3.find(R"("content_":")");
        CHECK(marker != std::string::npos);
        const std::size_t cjk_off = marker + 12;   // 跳过 "content_":" 共 12 字节
        CHECK(cjk_off == 52);
        CHECK(buf[6 + cjk_off + 0] == std::byte{0xE4});
        CHECK(buf[6 + cjk_off + 1] == std::byte{0xBD});
        CHECK(buf[6 + cjk_off + 2] == std::byte{0xA0});
    }

    // ---------------------------------------------------------------------
    // E4 —— encode 会回写 rmp.fh_.body_len_，这是有意的副作用
    // ---------------------------------------------------------------------
    void encode_sets_body_len_side_effect() {
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        core::RequestMessagePack rmp(fh, msg_gv1());
        rmp.fh_.body_len_ = 999;

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0x00});
        std::uint32_t          out_len = 0;

        CHECK(core::Encoder::encode(rmp, buf, out_len) == types::IoStatus::ok);
        CHECK(rmp.fh_.body_len_ == 119);
    }

    // ---------------------------------------------------------------------
    // E5 —— 越界写检测：帧尾之后的每一个字节都必须保持哨兵值
    // ---------------------------------------------------------------------
    void encode_does_not_write_past_frame() {
        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        core::RequestMessagePack rmp(fh, msg_gv1());

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0xAA});
        std::uint32_t          out_len = 0;

        CHECK(core::Encoder::encode(rmp, buf, out_len) == types::IoStatus::ok);
        CHECK(out_len == 125);
        for (std::size_t i = 125; i < 1024; ++i) {
            CHECK(buf[i] == std::byte{0xAA});
        }
    }

    // ---------------------------------------------------------------------
    // E6 —— 非法 UTF-8：dump(strict) 抛 type_error.316，必须被 encode 内部吃掉。
    //        修复前这里会因 noexcept 下逸出异常而 std::terminate 掉整个进程。
    // ---------------------------------------------------------------------
    void encode_rejects_invalid_utf8() {
        // 0xFF 不是合法 UTF-8 首字节。
        // 写成两段字面量拼接：C++ 的 \x 转义是贪婪的，"\xFEend" 会被读成 \xFEe（越界）。
        const std::string bad = std::string("bad\xFF\xFE" "end");
        CHECK(bad.size() == 8);

        types::RequestMsg m = msg_gv1();
        m.content_          = bad;

        FrameHeader              fh(core::Opcode::request, core::Status::ok);
        core::RequestMessagePack rmp(fh, m);
        rmp.fh_.body_len_ = 777;

        std::vector<std::byte> buf     = fresh_buf(1024, std::byte{0xAA});
        std::uint32_t          out_len = 12345;

        const types::IoStatus st = core::Encoder::encode(rmp, buf, out_len);

        // 能执行到这一行本身就是断言的一部分：进程没有被 terminate。
        CHECK(st == types::IoStatus::error);
        CHECK(out_len == 0);
        CHECK(buf[0] == std::byte{0xAA});   // out_buf 一字节未动
        CHECK(rmp.fh_.body_len_ == 777);    // 失败路径不回写 body_len_
    }

    // ---------------------------------------------------------------------
    // E7 —— encode 不做有效性校验：四个字符串全空也照常编码成功
    // ---------------------------------------------------------------------
    void encode_min_frame_empty_strings() {
        FrameHeader              fh(core::Opcode::ack, core::Status::ok);
        core::RequestMessagePack rmp(fh, msg_gv4());

        std::vector<std::byte> buf = fresh_buf(1024, std::byte{0x00});
        std::uint32_t          out_len = 0;

        const types::IoStatus st = core::Encoder::encode(rmp, buf, out_len);
        CHECK(st == types::IoStatus::ok);   // 不是 error：校验由上层负责
        CHECK(out_len == 98);
        CHECK(buf[0] == std::byte{0x00});
        CHECK(buf[1] == std::byte{0x00});
        CHECK(buf[5] == std::byte{0x5C});   // body_len = 92
        CHECK(body_of(buf, 6, 92) == kJsonGv4);
    }

    // ---------------------------------------------------------------------
    // E8 —— 编译期：encode 必须是 noexcept
    // ---------------------------------------------------------------------
    void encode_is_noexcept() {
        static_assert(noexcept(core::Encoder::encode(std::declval<core::RequestMessagePack&>(),
                                                     std::declval<std::vector<std::byte>&>(),
                                                     std::declval<std::uint32_t&>())),
                      "Encoder::encode must be noexcept");
        static_assert(FrameHeader::wire_size == 6, "wire header must be 6 bytes");
    }
} // namespace

int main() {
    verify_golden_vector_sizes();

    encode_golden_ascii();
    encode_golden_multibyte_len();
    encode_golden_utf8_cjk();
    encode_sets_body_len_side_effect();
    encode_does_not_write_past_frame();
    encode_rejects_invalid_utf8();
    encode_min_frame_empty_strings();
    encode_is_noexcept();

    std::printf("test_encoder: PASSED\n");
    return 0;
}
