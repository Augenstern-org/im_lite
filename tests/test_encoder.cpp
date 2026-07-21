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

    std::string result;

    core::Opcode op = core::Opcode::ack;
    core::Status st = core::Status::ok;

    auto encoder = core::Encoder::Encoder(op, st, msg, result);
    assert(encoder == types::IoStatus::ok);

    return 0;
}