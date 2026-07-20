//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_CORE_H
#define COM_LITE_CORE_H

#include <cstdint>
#include <functional>
#include "common/message.h"

namespace core {
    class Core {
    public:
        explicit Core(uint16_t port = 7891, int backlog = 2) noexcept;

        ~Core();

        Core(const Core&) = delete;

        Core& operator=(const Core&) = delete;

        bool init();
        void run();

        // 回调函数
        using message_handler    = std::function<void(int fd, types::RequestMsg)>;
        using disconnect_handler = std::function<void(int fd)>;

    private:
        void handle_accept();

        void handle_client(int fd);

        void close_client(int fd);

        uint16_t port_;
        int      backlog_;
        int      listen_fd_ = -1;
        int      epoll_fd_  = -1;
    };
} // namespace core
#endif // COM_LITE_CORE_H