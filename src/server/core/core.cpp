//
// Created by Neuroil on 2026/7/15.
//

#include "server/core/core.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include "common/frame_header.h"
#include "common/binary_code.h"
#include "common/encoder.h"
#include "common/decoder.h"

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
                for (int pending : pending_close_) {
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
            std::vector<std::byte>& in = conn.read_buffer();

            // 分帧推进 + 错误路径三连（docs/logs/2026-07-31.md §4.4）。四条承重理由：
            // (a) 必须 drain 到 incomplete，不设帧数上限。全仓无 EPOLLET（LT），on_readable 已把
            //     内核接收缓冲抽干到 EAGAIN；若只解一帧就退出，用户态 in 里的第二帧等不到新的
            //     EPOLLIN，会一直 stall 到对端再发字节。唯一的量化闸是 kReadSoftCap 64KiB。
            // (b) erase 必须先于 message_handler_。main.cpp:39-43 的 try/catch 包着整个 run()，
            //     handler 抛异常会栈展开出 handle_client；游标先推进则毒帧已出局，后推进就是
            //     下次再解同一帧再抛，构成 livelock。
            // (c) 循环体内 close_client 调用点为 0，所有杀决策只写 fatal = true; break;，
            //     EPOLLIN 分支收敛成恰好一个 close_client(fd); return;（全函数 6 处 → 3 处）。
            // (d) 不加读缓冲上限，恒界可推导：drain 退出时 in 中只剩一个未完成帧（< 6 字节，
            //     或 wire_size + k 且 k < body_len ≤ 65530）⇒ 残留 ≤ 65535；on_readable 每轮
            //     append ≤ 4096 且 size() >= 65536 即返回 ⇒ read_buf_ ≤ 69631 字节恒成立。
            //     承重前提：帧头未齐与 frame_too_long 校验必须先于帧体未齐校验（decoder.h 次序，
            //     S2 已在该文件落实为注释）。
            // 本分支不再清空读缓冲：字节消费由 ok 路径的精确 erase 承担；半帧重组由 incomplete
            // 原样保留字节 + 下面的 break 出口承担（下一次 EPOLLIN 把剩余字节追加到尾部凑齐）。
            bool fatal = false;
            std::vector<std::pair<MessagePack, int>> send_queue; // 每帧复用，循环体顶部先清空
            while (!in.empty()) {
                MessagePack     rmp;
                types::IoStatus dst = codec::Decoder::decode(in, rmp);
                if (dst == types::IoStatus::incomplete) break; // 唯一「保留字节」的出口
                if (dst != types::IoStatus::ok) { fatal = true; break; }

                // 先推进，再派发：当且仅当 Decoder::decode 返回 IoStatus::ok 时，本帧消耗
                // FrameHeader::wire_size + out_rmp.fh_.body_len_ 字节；其余返回值下该表达式无定义
                // （docs/logs/2026-07-31.md §4.3 契约原文，承重本行 erase）。
                in.erase(in.begin(), in.begin() + FrameHeader::wire_size + rmp.fh_.body_len_);
                // ↑ 此行之后，本轮循环再无「既不推进也不终止」的分支

                // 按 opcode 执行操作。回调判空（三连②）：未注册时不产出也不崩溃。
                send_queue.clear();
                if (message_handler_) {
                    message_handler_(fd, rmp, send_queue);
                }

                // 编码与发送融合成一个循环，每轮复用 encode_buf_：
                // encode_buf_ 跨消息、跨连接复用，由 encode() 按需增长（只增不减）；
                // conn.send() 会把数据拷进 write_buf_，因此编码缓冲的生命周期在 send() 返回时即结束，
                // 下一轮可以直接覆写。只有 [0, out_len) 会被发送，上一条消息留在尾部的残留字节永不上线。
                for (auto& item : send_queue) {
                    // 查询 conn
                    auto to_it = conns_.find(item.second);
                    if (to_it == conns_.end()) {
                        fatal = true;
                        break;
                    }
                    Connections& to_conn = to_it->second;

                    // 编码
                    uint32_t        out_len = 0;
                    types::IoStatus est     = codec::Encoder::encode(item.first, encode_buf_, out_len);
                    if (est != types::IoStatus::ok) {
                        fatal = true;
                        break;
                    }

                    types::IoStatus wst = to_conn.send(reinterpret_cast<const char*>(encode_buf_.data()), out_len);
                    if (wst == types::IoStatus::closed || wst == types::IoStatus::error) {
                        fatal = true;
                        break;
                    }
                }
                if (fatal) break;
            }

            if (fatal || rst == types::IoStatus::error ||
                (rst == types::IoStatus::closed && !conn.want_write())) {
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
        for (int fd : pending_close_) {
            conns_.erase(fd);
            if (disconnect_handler_) {
                disconnect_handler_(fd);
            }
        }
        pending_close_.clear();
    }
} // namespace core
