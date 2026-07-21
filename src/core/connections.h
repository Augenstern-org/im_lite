//
// Created by Neuroil on 2026/7/20.
//

#ifndef COM_LITE_CONNECTIONS_H
#define COM_LITE_CONNECTIONS_H

#include <cstddef>
#include <vector>
#include "common/io_status.h"

namespace core {
    class Connections {
    public:
        explicit Connections(int fd) noexcept;
        ~Connections();
        Connections(const Connections&) = delete;

        Connections& operator=(const Connections&) = delete;

        types::IoStatus on_readable();
        types::IoStatus on_writeable();
        types::IoStatus send(const char* data, std::size_t len);

        std::vector<std::byte>& read_buffer() noexcept { return read_buf_; }

        int  fd() const noexcept { return fd_; }
        bool want_write() const noexcept { return write_pos_ < write_buf_.size(); }
        bool peer_closed() const noexcept { return peer_closed_; }

    private:
        types::IoStatus flush();

        void compact() noexcept;

        std::vector<std::byte> read_buf_;
        std::vector<std::byte> write_buf_;
        std::size_t write_pos_   = 0;
        int         fd_          = -1;
        bool        peer_closed_ = false;
    };
}


#endif //COM_LITE_CONNECTIONS_H