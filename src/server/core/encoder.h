//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_ENCODER_H
#define COM_LITE_ENCODER_H

#include "common/message_pack.h"
#include "json.hpp"
#include "common/io_status.h"
#include "common/message.h"
#include <vector>
#include <netinet/in.h>
#include <cstddef>
#include <cstring>
#include <string>

namespace core {
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

        // body_encoder
        // 不是 noexcept：nlohmann::json 构造可能 std::bad_alloc，dump() 遇非法 UTF-8 抛 type_error.316。
        static uint32_t body_encoder(const Message& request_msg, std::vector<std::byte>& out_buf) {
            nlohmann::json j = request_msg;
            // 四个参数全部显式钉住，与当前 j.dump() 逐字节等价；防止上游默认值变更冲掉黄金向量。
            const std::string serialized = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
            std::memcpy(
                out_buf.data() + FrameHeader::wire_size,
                serialized.data(),
                serialized.size()
            );
            return static_cast<uint32_t>(serialized.size());
        }

    public:
        Encoder()  = default;
        ~Encoder() = default;

        // 出错后置条件：返回 error 时 out_buf 一字节未动 —— 所有可能抛出的操作（json 构造、dump）
        // 都在第一次写入之前完成。若将来重构为直接向 out_buf 流式写入，此保证必须重新审视。
        static types::IoStatus encode(
            MessagePack&            rmp,
            std::vector<std::byte>& out_buf,
            uint32_t&               out_len
        ) noexcept {
            // TODO：编码器不做有效性校验，由上层机制保证资源有效。
            // if (!rmp.is_valid()) return types::IoStatus::error;
            out_len = 0;
            try {
                const uint32_t len = body_encoder(rmp.msg_, out_buf);
                rmp.fh_.body_len_  = len;
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
} // core::encoder

#endif //COM_LITE_ENCODER_H
