//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_ENCODER_H
#define COM_LITE_ENCODER_H

#include "binary_code.h"
#include "frame_header.h"
#include "json.hpp"
#include "common/io_status.h"
#include "common/message.h"
#include "utils/endian.h"
#include <vector>

namespace core {
    class Encoder {
        /* -------------------------------------------------------
         * 定义转换规则
         *
         *      enum                =>         binary code
         * -------------------------------------------------------
         * Opcode::ack              =>              0
         * Opcode::request          =>              1
         * Opcode::response         =>              2
         *
         * Status::ok               =>              0
         * Status::fail             =>              1
         *
         * -------------------------------------------------------
         *
         * 帧头布局
         * -------------------------------------------------------
         * | opcode | status |     body_len     |       ...       |
         * -------------------------------------------------------
         *   1byte    1byte         4byte
         *
         */

        // 产生头
        inline std::vector<std::byte> build_header_except_body_len(FrameHeader fh) {

        }

        // 序列化
        inline std::string serialize(const types::RequestMsg& request_msg) {
            nlohmann::json j = request_msg;
            return j.dump();
        }

        // 建帧
        inline types::IoStatus build_frame(std::string& header, const std::string& request_msg_string) {
            if (request_msg_string.size() > UINT32_MAX) {
                return types::IoStatus::frame_too_long;
            }

            uint32_t len = utils::endian::to_big(request_msg_string.size());

            header.append(reinterpret_cast<const char*>(&len), sizeof(len));
            header.append(request_msg_string);
            return types::IoStatus::ok;
        }

        // 包装
        inline types::IoStatus Encoder(Opcode                   op,
                                       Status                   st,
                                       const types::RequestMsg& request_msg,
                                       std::string&             binary_out) {
            binary_out       = build_header_except_body_len(op, st);
            std::string body = serialize(request_msg);
            return build_frame(binary_out, body);
        }
    };
} // core::encoder

#endif //COM_LITE_ENCODER_H