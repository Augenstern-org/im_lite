//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USER_H
#define COM_LITE_USER_H

#include <vector>

namespace coordination {
    class User {
    private:
        // fds
        // std::vector<int> connections_;
        int fd_ = -1;

    public:
        bool get_fd(int& fd) const {
            fd = fd_;
            return true;
        }

        // bool get_fd(int& fd, int device_id) {
        //     // 目前仅支持单用户单fd
        //     // 启用多fd之后需查询具体使用什么fd(根据设备id)
        // }
    };
}

#endif //COM_LITE_USER_H
