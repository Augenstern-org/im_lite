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

        bool get_fd(int& fd, int device_id = -1) const {
            // 目前仅支持单用户单fd
            // 启用多fd之后需查询具体使用什么fd(根据设备id)
            if (fds_.empty()) return false;
            if (device_id == -1) {
                fd = fds_[0];
                return true;
            }
            // 其他查找逻辑
            return true;
        }

        void add(int fd) {
            fds_.push_back(fd);
        }

        std::vector<int>& vec() {
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
