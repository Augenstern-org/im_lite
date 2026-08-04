//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USER_H
#define COM_LITE_USER_H

// #include <vector>

namespace coordination {
    class User {
    private:
        // fds
        // std::vector<int> connections_;
        int fd_ = -1;

    public:
        explicit User(int fd) : fd_(fd) {}

        ~User() = default;

        // 通过引用传递fd，返回值仅表示是否成功
        bool get_fd(int& fd) const {
            if (fd_ == -1) return false;
            fd = fd_;
            return true;
        }

        // bool get_fd(int& fd, int device_id) {
        //     // 目前仅支持单用户单fd
        //     // 启用多fd之后需查询具体使用什么fd(根据设备id)
        // }

        // 向量化之后需要实现的函数
        // void add(int fd);
        // std::vector<int>& vec();
        // bool delete_device(const std::string& device_id);
    };
}

#endif //COM_LITE_USER_H
