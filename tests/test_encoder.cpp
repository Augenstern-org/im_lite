//
// Created by Neuroil on 2026/7/21.
//

#include <cassert>
#include <cstdio>
#include "core/encoder.h"

int main() {
    types::RequestMsg msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = "Neuroil";
    msg.to_uid_        = "Evil";
    msg.client_msg_id_ = "";
    msg.content_       = "Hello World!";

    std::vector<std::byte> result;
    result.resize(1024);

    FrameHeader fh(core::Opcode::ack, core::Status::ok);

    core::RequestMessagePack rmp(fh, msg);

    auto status = core::Encoder::encode(rmp, result);
    assert(status == types::IoStatus::error);

    rmp.request_msg_.client_msg_id_ = "hash";

    assert(core::Encoder::encode(rmp, result) == types::IoStatus::ok);

    return 0;
}
