//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_CORE_H
#define COM_LITE_CORE_H

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/message_pack.h"
#include "server/core/connections.h"

namespace core {
    class Core {
    public:
        explicit Core(uint16_t port = 7891, int backlog = 2) noexcept;
        ~Core();
        Core(const Core&)            = delete;
        Core& operator=(const Core&) = delete;

        bool init();
        void run();

        // 回调函数
        using message_handler = std::function<void(
            int                                       fd,
            const MessagePack&                        in,
            std::vector<std::pair<MessagePack, int>>& out_queue
        )>;
        using disconnect_handler = std::function<void(int fd)>;

        void set_message_handler(message_handler h) { message_handler_ = std::move(h); }
        void set_disconnect_handler(disconnect_handler h) { disconnect_handler_ = std::move(h); }

    private:
        void handle_accept();
        void handle_client(int fd, uint32_t events);
        void close_client(int fd);

        void update_events(int fd, const Connections& conn);
        void drain_pending_close();

        uint16_t port_;
        int      backlog_;
        int      listen_fd_ = -1;
        int      epoll_fd_  = -1;

        std::unordered_map<int, Connections> conns_;
        std::vector<int>                     pending_close_;
        message_handler                      message_handler_;
        disconnect_handler                   disconnect_handler_;
        // 出站编码的复用缓冲：跨消息、跨连接共用一块，由 Encoder::encode() 按需增长（只增不减）。
        std::vector<std::byte>               encode_buf_;
    };
} // namespace core
#endif // COM_LITE_CORE_H
