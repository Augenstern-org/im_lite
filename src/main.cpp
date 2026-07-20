//
// Created by Neuroil on 2026/7/14.
//

#include <iostream>

#include "controller/controller.h"
#include "core/core.h"

int main() {
    // 初始化 Core Controller UsersGroup
    // Coordination 仅仅是业务代码，不具有对象

    core::Core core(7891, 2);
    if (!core.init()) {
        return -1;
    }

    controller::Controller controller;

    // 核心层只上报 fd 断开，由上层决定做什么（步 4 后此处改为驱动 UsersGroup 解绑）
    core.set_disconnect_handler([](int fd) {
        std::cout << "Client disconnected: " << fd << std::endl;
    });

    core.run();

    return 0;
}