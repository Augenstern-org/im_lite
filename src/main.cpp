//
// Created by Neuroil on 2026/7/14.
//

#include <cerrno>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "socket failed!" << std::endl;
        return -1;
    }

    // 设置 SO_REUSEADDR 避免端口占用
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(7891);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind failed: " << strerror(errno) << std::endl;
        return -1;
    }

    if (listen(server_fd, 2) < 0) {
        std::cerr << "listen failed: " << strerror(errno) << std::endl;
        return -1;
    }

    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd <= 0) {
        std::cerr << "epoll_fd failed!" << std::endl;
        return -1;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    std::cout << "正在监听端口 7891:" << std::endl;

    epoll_event events[64];
    char buf[1024];
    for (;;) {
        int nfds = epoll_wait(epoll_fd, events, 64, -1);
        if (nfds < 0) {
            std::cerr << "epoll_wait() failed: " << strerror(errno) << "\n";
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                for (;;) {
                    int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            std::cerr << "accept error: " << strerror(errno) << "\n";
                        }
                        break;
                    }
                    std::cout << "New client connected: " << client_fd << "\n";

                    // 修正：正确的非阻塞设置
                    int client_flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, client_flags | O_NONBLOCK);

                    epoll_event client_event{};
                    client_event.events = EPOLLIN;
                    client_event.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
                }
            } else {
                int n = read(fd, buf, sizeof(buf) - 1);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;  // 没有数据可读
                    }
                    std::cout << "Client " << fd << " error: " << strerror(errno) << "\n";
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                } else if (n == 0) {
                    std::cout << "Client " << fd << " disconnected\n";
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                } else {
                    buf[n] = '\0';
                    send(fd, buf, n, 0);
                }
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
    return 0;
}