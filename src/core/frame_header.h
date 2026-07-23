//
// Created by Neuroil on 2026/7/21.
//

#ifndef COM_LITE_FRAME_HEADER_H
#define COM_LITE_FRAME_HEADER_H

#include "binary_code.h"

/*
 *
 * 帧头布局
 * -------------------------------------------------------
 * | opcode | status |     body_len     |       ...       |
 * -------------------------------------------------------
 *   1byte    1byte         4byte
 *
 */

// 这里初始化逻辑不太好，之后看看有没有更好的

struct FrameHeader {
    core::Opcode opcode_   = core::Opcode::_init_;
    core::Status status_   = core::Status::_init_;
    uint32_t     body_len_ = 0;

    FrameHeader() = default;

    explicit FrameHeader(core::Opcode opcode, core::Status status) : opcode_(opcode), status_(status) {}

    // op 与 st 均已赋值才可以
    bool is_valid() const { return opcode_ != core::Opcode::_init_ && status_ != core::Status::_init_; }
};

#endif //COM_LITE_FRAME_HEADER_H