//
// Created by Neuroil on 2026/7/14.
//

#include <iostream>
#include <utility>
#include <vector>

#include "common/message_pack.h"
#include "server/controller/controller.h"
#include "server/coordination/assembler.h"
#include "server/core/core.h"

int main() {
    // 声明顺序即所有权顺序：Core 持有捕获了 &controller 的 lambda，
    // 反序析构保证 Core 先于被捕获对象销毁（C++ Core Guidelines F.53）。
    coordination::Assembler  assembler;
    coordination::UsersGroup users_group;

    // 演示用户表：uid -> 登录凭证。真实认证（数据库 / 外部鉴权）落地前先静态查表。
    const controller::Controller::UserTable users = {
        {"alice", "alice123"},
        {"bob",   "bob456"},
    };

    controller::Controller controller(assembler, users_group, users);

    core::Core core(7891, 2);
    if (!core.init()) {
        return -1;
    }

    // 核心层只上报 fd 断开，由上层决定做什么（断开即从用户组解绑，防 fd 复用串消息）。
    core.set_disconnect_handler(
        [&controller](int fd) {
            std::cout << "Client disconnected: " << fd << std::endl;
            controller.delete_fd(fd);
        }
    );
    // 注册消息处理接口
    core.set_message_handler(
        [&controller](int fd, const MessagePack& in, std::vector<std::pair<MessagePack, int>>& out_queue) {
            controller.process(fd, in, out_queue);
        }
    );

    try {
        core.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
