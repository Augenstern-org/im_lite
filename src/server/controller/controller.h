//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_CONTROLLER_H
#define COM_LITE_CONTROLLER_H

#include <functional>
#include <utility>
#include <vector>

#include "server/coordination/assembler.h"
#include "server/coordination/users_group.h"
#include "common/message_pack.h"

namespace controller {
    class Controller {
        // static void request(int fd, const Message& msg, std::vector<MessagePack>& out_queue) {
        //     //
        // }
        //
        // static void ack(int fd, std::vector<MessagePack>& out_queue) {
        //     // 直接返回应答
        // }

        // using AckAssembler = std::function<types::IoStatus(/*interface*/)>;
        using ResAssembler = std::function<types::IoStatus(/*interface*/)>;

        // static AckAssembler ack_assembler_;
        static ResAssembler res_assembler_;

    public:
        Controller() = default;

        static void process(int fd, const MessagePack& rmp, std::vector<std::pair<MessagePack, int>>& out_queue);

        // static void set_ack_assembler(AckAssembler a) { ack_assembler_ = std::move(a); }
        static void set_res_assembler(ResAssembler r) { res_assembler_ = std::move(r); }
    };
} // controller

#endif //COM_LITE_CONTROLLER_H
