//
// Created by Neuroil on 2026/7/20.
//

#ifndef COM_LITE_CONNETIONS_H
#define COM_LITE_CONNETIONS_H

#include <cstddef>
#include <string>
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

        int fd() const noexcept {return fd_;}
        std::string& read_buffer() noexcept {return read_buf_;}

        bool want_write() const noexcept {return write_pos_ < write_buf_.size();}

    private:
        types::IoStatus flush();

        std::string read_buf_;
        std::string write_buf_;
        std::size_t write_pos_ = 0;
        int fd_ = -1;

    };
}


#endif //COM_LITE_CONNETIONS_H
