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

        // MessagePack -> Encoder -> connection.write_buf_
        // 在编码器缓冲区拼接，拼接完成之后写入输出缓冲区；
        // MessagePack 在编码之后就当成二进制对待了，应该使用 std::vector<std::byte> 来存储。

        // 先去学模板编程吧，回来再考虑怎么完成 MessagePack 到 FrameHeader & FrameBody 并编码解码
        // 编码解码器自动通过协议号识别转换方式
    };
} // core::encoder

#endif //COM_LITE_ENCODER_H