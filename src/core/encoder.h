//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_ENCODER_H
#define COM_LITE_ENCODER_H

#include "binary_code.h"
#include "message_pack.h"
#include "json.hpp"
#include "common/io_status.h"
#include "common/message.h"
#include "utils/endian.h"
#include <vector>

namespace core {
    class Encoder {
        // MessagePack -> Encoder -> connection.write_buf_
        // 在编码器缓冲区拼接，拼接完成之后写入输出缓冲区；
        // MessagePack 在编码之后就当成二进制对待了，应该使用 std::vector<std::byte> 来存储。

        // 先去学模板编程吧，回来再考虑怎么完成 MessagePack 到 FrameHeader & FrameBody 并编码解码
        // 编码解码器自动通过协议号识别转换方式（暂不考虑）

        // 编译期计算出帧头大小
        static constexpr std::size_t no_padding_size =
            sizeof(decltype(FrameHeader::opcode_))
            + sizeof(decltype(FrameHeader::status_))
            + sizeof(decltype(FrameHeader::body_len_));

        // 内部缓冲区
        // std::vector<std::byte> buf_;

        // header_encoder
        static types::IoStatus header_encoder(const FrameHeader& fh, std::vector<std::byte>& out_buf) noexcept {
            out_buf[0] = static_cast<std::byte>(fh.opcode_);
            out_buf[1] = static_cast<std::byte>(fh.status_);

            if (fh.body_len_ == 0) return types::IoStatus::error;
            uint32_t safe_len = utils::endian::to_big(fh.body_len_);
            memcpy(out_buf.data() + 2, &safe_len, sizeof(safe_len));
            return types::IoStatus::ok;
        }

        // body_encoder
        static void body_encoder(const types::RequestMsg& request_msg, std::vector<std::byte>& out_buf, uint32_t& len) noexcept {
            nlohmann::json j = request_msg;
            std::string serialized_data = j.dump();
            memcpy(
                out_buf.data() + no_padding_size,
                serialized_data.data(),
                serialized_data.size()
            );
            len = serialized_data.size();
        }

    public:
        Encoder() = default;
        ~Encoder() = default;

        static types::IoStatus encode(RequestMessagePack& request_message_pack, std::vector<std::byte>& out_buffer) noexcept {
            // TODO：目前为无效自行抛出error，以后由其他机制保证资源有效。
            if (!request_message_pack.is_valid()) return types::IoStatus::error;

            // 编码逻辑
            uint32_t len;
            body_encoder(request_message_pack.request_msg_, out_buffer, len);
            request_message_pack.fh_.body_len_ = len;
            return header_encoder(request_message_pack.fh_, out_buffer);
        }
    };
} // core::encoder

#endif //COM_LITE_ENCODER_H