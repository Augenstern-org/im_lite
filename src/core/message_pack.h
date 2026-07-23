//
// Created by Neuroil on 2026/7/22.
//

#ifndef COM_LITE_MESSAGE_PACK_H
#define COM_LITE_MESSAGE_PACK_H

#include "common/message.h"
#include "frame_header.h"

namespace core {
    struct RequestMessagePack {
        FrameHeader       fh_;
        types::RequestMsg request_msg_;

        RequestMessagePack() = default;

        explicit RequestMessagePack(const types::RequestMsg& request_msg) {
            fh_ = {};
            request_msg_ = std::move(request_msg);
        }

        explicit RequestMessagePack(const FrameHeader fh) : fh_(fh) {
            request_msg_ = {};
        }

        explicit RequestMessagePack(const FrameHeader fh, const types::RequestMsg& request_msg) : fh_(fh) {
            request_msg_ = std::move(request_msg);
        }

        // 同样的 is_init .其实我觉得 is_valid 这个名字好像更好一点
        // 已经改了
        bool is_valid() { return fh_.is_valid() && request_msg_.is_valid(); }
    };
}

#endif //COM_LITE_MESSAGE_PACK_H
