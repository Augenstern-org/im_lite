//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USERSGROUP_H
#define COM_LITE_USERSGROUP_H

#include <cstdint>
#include <unordered_map>
#include "user.h"

namespace coordination {
    class UsersGroup {
        // 之后采用数据库储存
        // uid <-> fd
        std::unordered_map<uint32_t, User> group_;
    };
} // coordination

#endif //COM_LITE_USERSGROUP_H
