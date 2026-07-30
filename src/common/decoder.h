//
// Created by Neuroil on 2026/7/23.
//

#ifndef COM_LITE_DECODER_H
#define COM_LITE_DECODER_H

#include "common/binary_code.h"
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
#include <utility>

namespace codec {
    class Decoder {
    public:
        // 分帧逻辑应当在 decoder 之外就完成了
        static types::IoStatus decode(
            const std::vector<std::byte>& in_buf,
            MessagePack&                  out_rmp
        ) noexcept {
            try {
                if (in_buf.size() < FrameHeader::wire_size) return types::IoStatus::error;

                // 枚举值域校验先于 static_cast
                const auto raw_opcode = static_cast<std::uint8_t>(in_buf[0]);
                const auto raw_status = static_cast<std::uint8_t>(in_buf[1]);
                if (!types::is_known_opcode(raw_opcode) || !types::is_known_status(raw_status)) {
                    return types::IoStatus::error;
                }

                std::uint32_t body_len = 0;
                std::memcpy(&body_len, in_buf.data() + 2, sizeof(body_len));
                body_len = ntohl(body_len);

                if (body_len > max_message_body_length) return types::IoStatus::frame_too_long;
                if (in_buf.size() - FrameHeader::wire_size < body_len) return types::IoStatus::error;

                const std::string s(
                    reinterpret_cast<const char*>(in_buf.data()) + FrameHeader::wire_size,
                    body_len
                );

                nlohmann::json j = nlohmann::json::parse(s, nullptr, /*allow_exceptions=*/false);
                if (j.is_discarded()) return types::IoStatus::error; // 必须调方法，不能与 value_t 比较
                if (!j.is_object()) return types::IoStatus::error;   // "[1,2,3]" / "null" 会通过 parse

                Message msg = j.get<Message>(); // 不用 get_to：失败时不半写出参
                if (!msg.is_valid()) return types::IoStatus::error;

                FrameHeader fh;
                fh.opcode_   = static_cast<types::Opcode>(raw_opcode);
                fh.status_   = static_cast<types::Status>(raw_status);
                fh.body_len_ = body_len;

                // 执行最终操作（不能再抛异常）
                out_rmp.fh_  = fh;
                out_rmp.msg_ = std::move(msg);
                return types::IoStatus::ok;
            } catch (...) {
                return types::IoStatus::error;
            }
        }
    };
} // codec

#endif //COM_LITE_DECODER_H
