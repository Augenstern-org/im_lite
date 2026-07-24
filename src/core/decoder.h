//
// Created by Neuroil on 2026/7/23.
//

#ifndef COM_LITE_DECODER_H
#define COM_LITE_DECODER_H

#include "binary_code.h"
#include "message_pack.h"
#include "json.hpp"
#include "common/io_status.h"
#include "common/message.h"
#include <vector>
#include <netinet/in.h>
#include "encoder.h"

namespace core {
    class Decoder {
    public:
        // 分帧逻辑应当在 decoder 之外就完成了
        static types::IoStatus decode(const std::vector<std::byte>& in_buf, RequestMessagePack& request_message_pack) {
            FrameHeader       fh;
            types::RequestMsg msg;

            fh.opcode_ = static_cast<Opcode>(in_buf[0]);
            fh.status_ = static_cast<Status>(in_buf[1]);
            memcpy(&fh.body_len_, in_buf.data() + 2, sizeof(decltype(FrameHeader::body_len_)));

            if (!fh.is_valid()) return types::IoStatus::error;

            // 逆序列化
            std::string s;
            s.resize(in_buf.size() - Encoder::no_padding_size);
            memcpy(s.data(), in_buf.data() + Encoder::no_padding_size, s.size());

            msg = nlohmann::json::parse(s);

            if (!msg.is_valid()) return types::IoStatus::error;

            request_message_pack.fh_ = fh;
            request_message_pack.request_msg_ = msg;

            return types::IoStatus::ok;
        }
    };
}

#endif //COM_LITE_DECODER_H
