//
// Created by Neuroil on 2026/7/14.
//

#include "core/core.h"

int main() {
    core::Core core(7891, 2);
    if (!core.init()) {
        return -1;
    }
    core.run();
    return 0;
}