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
    // 目前为单线程设计
    class UsersGroup {
        // 之后采用数据库储存
        // uid <-> user(fds)
        std::unordered_map<std::string, User> group_;

        void delete_user(const std::string& uid) {
            group_.erase(uid);
        }

    public:
        void register_user(const std::string& uid, int fd) noexcept {
            // 添加第一个 fd
            if (group_.try_emplace(uid, User(fd)).second) return;
            // 使用向量储存之后允许为同一 uid 继续添加 fd
            // group_[uid].add(fd);
        }

        bool delete_fd(const std::string& uid/*, const std::string& device_id*/) {
            auto it = group_.find(uid);
            if (it == group_.end()) return false;
            User& user = it->second;
            // 使用向量储存之后，单个元素直接调用析构函数
            // if (user.vec().size() == 1) delete_user();
            // 否则仅删除对应设备的 fd
            // user.delete_device(device_id);
            delete_user(uid);
            return true;
        }

        bool query(const std::string& uid, int& fd) const {
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
