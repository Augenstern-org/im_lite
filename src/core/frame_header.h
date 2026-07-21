//
// Created by Neuroil on 2026/7/21.
//

#ifndef COM_LITE_FRAME_HEADER_H
#define COM_LITE_FRAME_HEADER_H

#include "binary_code.h"

struct FrameHeader {
    core::Opcode opcode_;
    core::Status status_;
    uint32_t     body_len_;
};

#endif //COM_LITE_FRAME_HEADER_H