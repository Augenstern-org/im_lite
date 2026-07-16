//
// Created by Neuroil on 2026/7/15.
//

#include "core.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

namespace core {

    Core::Core(uint16_t port, int backlog) noexcept : port_(port), backlog_(backlog) {}

    Core::~Core() {
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
        }
        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }
    }

    bool Core::init() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ == -1) {
            std::cerr << "socket failed!" << std::endl;
            return false;
        }

        // 设置 SO_REUSEADDR 避免端口占用
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "bind failed: " << strerror(errno) << std::endl;
            return false;
        }

        if (listen(listen_fd_, backlog_) < 0) {
            std::cerr << "listen failed: " << strerror(errno) << std::endl;
            return false;
        }

        fcntl(listen_fd_, F_SETFL, fcntl(listen_fd_, F_GETFL) | O_NONBLOCK);

        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            std::cerr << "epoll_fd failed!" << std::endl;
            return false;
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = listen_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);

        std::cout << "正在监听端口 " << port_ << ":" << std::endl;
        return true;
    }

    void Core::run() {
        epoll_event events[64];
        for (;;) {
            int nfds = epoll_wait(epoll_fd_, events, 64, -1);
            if (nfds < 0) {
                std::cerr << "epoll_wait() failed: " << strerror(errno) << "\n";
                break;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                if (fd == listen_fd_) {
                    handleAccept();
                } else {
                    handleClient(fd);
                }
            }
        }
    }

    void Core::handleAccept() {
        for (;;) {
            int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "accept error: " << strerror(errno) << "\n";
                }
                break;
            }
            std::cout << "New client connected: " << client_fd << "\n";

            int client_flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, client_flags | O_NONBLOCK);

            epoll_event client_event{};
            client_event.events = EPOLLIN;
            client_event.data.fd = client_fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event);
        }
    }

    void Core::handleClient(int fd) {
        char buf[1024];
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // 没有数据可读
            }
            std::cout << "Client " << fd << " error: " << strerror(errno) << "\n";
            closeClient(fd);
        } else if (n == 0) {
            std::cout << "Client " << fd << " disconnected\n";
            closeClient(fd);
        } else {
            buf[n] = '\0';
            send(fd, buf, n, 0);
        }
    }

    void Core::closeClient(int fd) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }

} // namespace core
