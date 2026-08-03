//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USERSGROUP_H
#define COM_LITE_USERSGROUP_H

// #include <cstdint>
#include <string>
#include <unordered_map>
#include "server/coordination/user.h"

namespace coordination {
    class UsersGroup {
        // 之后采用数据库储存
        // uid <-> user(fds)
        std::unordered_map<std::string, User> group_;

    public:
        bool query(const std::string& uid, int& fd) {
            // 无用户
            auto find = group_.find(uid);
            if (group_.end() == find) return false;

            // 无fd
            // 其实这里可能过度防御了，不过性能开销不大，就加上了
            int _fd = -1;
            if (!find->second.get_fd(_fd)) return false;

            // 正常返回
            fd = _fd;
            return true;
        }
    };
} // coordination

#endif //COM_LITE_USERSGROUP_H
