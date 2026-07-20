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
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port_);

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
        event.events  = EPOLLIN;
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
                    handle_accept();
                    continue;
                }
                bool skip = false;
                for (int pending: pending_close_) {
                    if (pending == fd) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    handle_client(fd, events[i].events);
                }
            }
            drain_pending_close();
        }
    }

    void Core::handle_accept() {
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
            client_event.events  = EPOLLIN;
            client_event.data.fd = client_fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event);

            conns_.try_emplace(client_fd, client_fd);
        }
    }

    void Core::handle_client(int fd, uint32_t events) {
        auto it = conns_.find(fd);
        if (it == conns_.end()) {
            return;
        }
        Connections& conn = it->second;

        if (events & (EPOLLHUP | EPOLLERR)) {
            close_client(fd);
            return;
        }

        // 先排空写缓冲，为随后新读入的字节让出高水位余量
        if (events & EPOLLOUT) {
            types::IoStatus st = conn.on_writeable();
            if (st == types::IoStatus::closed || st == types::IoStatus::error) {
                close_client(fd);
                return;
            }
        }

        if (events & EPOLLIN) {
            types::IoStatus rst = conn.on_readable();

            // 回显先于读状态分派：on_readable() 可能在同一次调用里既追加数据又返回 closed
            std::string& in = conn.read_buffer();
            if (!in.empty()) {
                types::IoStatus wst = conn.send(in.data(), in.size());
                in.clear();
                if (wst == types::IoStatus::closed || wst == types::IoStatus::error) {
                    close_client(fd);
                    return;
                }
            }

            if (rst == types::IoStatus::error) {
                close_client(fd);
                return;
            }
            // 对端已发 FIN 但仍在读回显，写缓冲排空后由 on_writeable() 收尾
            if (rst == types::IoStatus::closed && !conn.want_write()) {
                close_client(fd);
                return;
            }
        }

        update_events(fd, conn);
    }

    void Core::update_events(int fd, const Connections& conn) {
        // EPOLL_CTL_MOD 会整体替换事件掩码，因此每次都从连接状态重算完整掩码
        uint32_t mask = conn.peer_closed() ? 0u : static_cast<uint32_t>(EPOLLIN);
        if (conn.want_write()) {
            mask |= EPOLLOUT;
        }
        if (mask == 0) {
            close_client(fd);
            return;
        }
        epoll_event ev{};
        ev.events  = mask;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }

    // 延迟销毁
    void Core::close_client(int fd) {
        // EPOLL_CTL_DEL 必须早于 close()，且 close() 交由 Connections 析构在 erase 时完成
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        pending_close_.push_back(fd);
    }

    // 延迟销毁
    void Core::drain_pending_close() {
        for (int fd: pending_close_) {
            conns_.erase(fd);
            if (disconnect_handler_) {
                disconnect_handler_(fd);
            }
        }
        pending_close_.clear();
    }
} // namespace core