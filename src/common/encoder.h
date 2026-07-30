//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_ENCODER_H
#define COM_LITE_ENCODER_H

#include "common/message_pack.h"
#include "json.hpp"
#include "common/io_status.h"
#include "common/message.h"
#include "common/frame_header.h"
#include <vector>
#include <netinet/in.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace codec {
    class Encoder {
        // MessagePack -> Encoder -> connection.write_buf_
        // 在编码器缓冲区拼接，拼接完成之后写入输出缓冲区；
        // MessagePack 在编码之后就当成二进制对待了，应该使用 std::vector<std::byte> 来存储。

        // 先去学模板编程吧，回来再考虑怎么完成 MessagePack 到 FrameHeader & FrameBody 并编码解码
        // 编码解码器自动通过协议号识别转换方式（暂不考虑）

        // 内部缓冲区
        // std::vector<std::byte> buf_;

        // header_encoder
        static void header_encoder(const FrameHeader& fh, std::vector<std::byte>& out_buf) noexcept {
            out_buf[0] = static_cast<std::byte>(fh.opcode_);
            out_buf[1] = static_cast<std::byte>(fh.status_);

            uint32_t safe_len = htonl(fh.body_len_);
            std::memcpy(out_buf.data() + 2, &safe_len, sizeof(safe_len));
        }

        // body_serializer
        // 只做序列化、不碰 out_buf：帧体长度必须在任何写入之前就已知，encode() 才能在越界
        // 发生之前拦下超长帧。写入职责在 body_writer。
        // 不是 noexcept：nlohmann::json 构造可能 std::bad_alloc，dump() 遇非法 UTF-8 抛 type_error.316。
        static std::string body_serializer(const Message& request_msg) {
            nlohmann::json j = request_msg;
            // 四个参数全部显式钉住，与当前 j.dump() 逐字节等价；防止上游默认值变更冲掉黄金向量。
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
        }

        // body_writer
        // 前置条件由 encode() 的长度校验保证；本函数只写不查。
        static void body_writer(const std::string& serialized, std::vector<std::byte>& out_buf) noexcept {
            std::memcpy(
                out_buf.data() + FrameHeader::wire_size,
                serialized.data(),
                serialized.size()
            );
        }

    public:
        Encoder()  = default;
        ~Encoder() = default;

        // 出错后置条件：返回 error / frame_too_long 时 out_buf 一字节未动、rmp.fh_.body_len_ 不回写
        // —— 所有可能抛出的操作（json 构造、dump）与帧体长度校验都排在第一次写入之前。
        // 若将来重构为直接向 out_buf 流式写入，此保证必须重新审视。
        static types::IoStatus encode(
            MessagePack&            rmp,
            std::vector<std::byte>& out_buf,
            uint32_t&               out_len
        ) noexcept {
            // TODO：编码器不做有效性校验，由上层机制保证资源有效。
            // if (!rmp.is_valid()) return types::IoStatus::error;
            out_len = 0;
            try {
                const std::string serialized = body_serializer(rmp.msg_);
                // 阈值是「帧体字节数」，与 FrameHeader::body_len_ 同语义（docs/architecture.md §2.4：
                // body_len 不含 6 字节头），也与 Decoder 的同一道校验对齐。
                // 校验必须先于 body_writer：out_buf 由调用方按 wire_size + max_message_body_length
                // 分配，超长时先写后查等于越界已经发生。比较也必须先于向 uint32_t 收窄。
                if (serialized.size() > max_message_body_length) return types::IoStatus::frame_too_long;

                const auto len    = static_cast<uint32_t>(serialized.size());
                rmp.fh_.body_len_ = len;
                body_writer(serialized, out_buf);
                header_encoder(rmp.fh_, out_buf);
                out_len = static_cast<uint32_t>(FrameHeader::wire_size) + len;
                return types::IoStatus::ok;
            } catch (...) {
                // 必须 catch (...)：nlohmann 的异常基类不覆盖 std::bad_alloc，
                // 漏出去会在 noexcept 下触发 std::terminate。
                out_len = 0;
                return types::IoStatus::error;
            }
        }
    };
} // codec

#endif //COM_LITE_ENCODER_H
