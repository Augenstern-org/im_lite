//
// Created by Neuroil on 2026/7/14.
//

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

    core.run();

    return 0;
}