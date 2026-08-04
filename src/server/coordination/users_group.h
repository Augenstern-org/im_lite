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
    // 目前为单线程设计，详见 TODO.md
    class UsersGroup {
        // 之后采用数据库储存
        // uid <-> user(fds)
        std::unordered_map<std::string, User> uid_to_fd_;
        std::unordered_map<int, std::string> fd_to_uid_;

        void delete_user(const std::string& uid) noexcept {
            // 该函数仅在 delete_fd的最后调用
            // 因此不需要执行 fd 表的操作
            int fd = -1;
            uid_to_fd_.at(uid).get_fd(fd);
            uid_to_fd_.erase(uid);
        }

    public:
        bool register_user(const std::string& uid, int fd) noexcept {
            // fd 始终唯一
            fd_to_uid_.emplace(fd, uid);

            // 添加 uid 表的第一个 fd
            if (uid_to_fd_.try_emplace(uid, fd).second) return true;
            // 使用向量储存之后允许为同一 uid 继续添加 fd
            // uid_to_fd_[uid].add(fd);
            return true;
        }

        bool delete_fd(int fd) {
            // fd -> uid
            auto fd_it = fd_to_uid_.find(fd);
            if (fd_it == fd_to_uid_.end()) return false;
            const std::string& uid = fd_it->second;

            // uid -> User
            auto it = uid_to_fd_.find(uid);
            if (it == uid_to_fd_.end()) return false;

            // 在这行之后需保证不会出现异常
            fd_to_uid_.erase(fd_it);

            // 管理 uid 表
            // 使用向量储存之后，单个元素直接调用析构函数
            User& user = it->second;
            // if (user.vec().size() == 1) delete_user(uid);

            // 否则仅删除对应设备的 fd
            // user.delete_device(fd);
            delete_user(uid);
            return true;
        }

        bool query(const std::string& uid, int& fd) const {
            // 无用户
            auto find = uid_to_fd_.find(uid);
            if (uid_to_fd_.end() == find) return false;

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
