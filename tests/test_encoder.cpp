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
    msg.client_msg_id_ = "hash";
    msg.content_       = "Hello World!";

    std::vector<std::byte> result;
    result.resize(1024);

    FrameHeader fh(core::Opcode::ack, core::Status::ok);

    core::RequestMessagePack rmp(fh, msg);

    core::Encoder encoder;

    auto status = encoder.encode(rmp, result);
    assert(status == types::IoStatus::ok);

    return 0;
}