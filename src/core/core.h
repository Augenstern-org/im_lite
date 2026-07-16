//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_CORE_H
#define COM_LITE_CORE_H

#include <cstdint>

namespace core {

    class Core {
    public:
        explicit Core(uint16_t port = 7891, int backlog = 2) noexcept;
        ~Core();

        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        bool init();   // socket/bind/listen/非阻塞/epoll 装配；失败打印 stderr 返回 false
        void run();    // 阻塞事件循环，epoll_wait 失败返回

    private:
        void handleAccept();
        void handleClient(int fd);
        void closeClient(int fd);

        uint16_t port_;
        int      backlog_;
        int      listen_fd_ = -1;
        int      epoll_fd_  = -1;
    };

} // namespace core
#endif // COM_LITE_CORE_H
