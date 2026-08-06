//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USER_H
#define COM_LITE_USER_H

#include <algorithm>
#include <vector>

namespace coordination {
    class User {
    private:
        // fds
        std::vector<int> fds_;
        // int fd_ = -1;

    public:
        User() = default;
        explicit User(int fd) {
            fds_.push_back(fd);
        }

        ~User() = default;

        // 通过引用传递fd，返回值仅表示是否成功
        // bool get_fd(int& fd) const {
        //     if (fd_ == -1) return false;
        //     fd = fd_;
        //     return true;
        // }

        // 取该用户当前可用的 fd（最早注册且仍在线的那个）。
        // 目前投递面向连接而非设备，调用方不需要指定设备；多设备扇出落地时需要的是
        // get_fds(std::vector<int>&)，而不是 device_id 标量 —— 保留一个没有调用方
        // 能有意义传入的参数，只会把「分支为空却返回 true」这类契约违规留在原地。
        bool get_fd(int& fd) const {
            if (fds_.empty()) return false;
            fd = fds_[0];
            return true;
        }

        void add(int fd) {
            fds_.push_back(fd);
        }

        std::vector<int>& vec() {
            return fds_;
        }

        // 只读重载：供 UsersGroup 的不变式校验等 const 场景使用
        const std::vector<int>& vec() const {
            return fds_;
        }

        bool delete_device(int fd) {
            auto it = std::find(fds_.begin(), fds_.end(), fd);

            if (it == fds_.end()) return false;

            fds_.erase(it);
            return true;
        }
    };
}

#endif //COM_LITE_USER_H
