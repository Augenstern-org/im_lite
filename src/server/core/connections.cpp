//
// Created by Neuroil on 2026/7/20.
//

#include "connections.h"

#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>

namespace {
    constexpr std::size_t kReadSoftCap      = 64 * 1024;
    constexpr std::size_t kWriteHighWater   = 1024 * 1024;
    constexpr std::size_t kCompactThreshold = 8 * 1024;
}

namespace core {
    Connections::Connections(int fd) noexcept : fd_(fd) {
        //
    }

    Connections::~Connections() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    types::IoStatus Connections::on_readable() {
        char buf[4096];
        for (;;) {
            ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n > 0) {
                read_buf_.insert(read_buf_.end(),
                                 reinterpret_cast<const std::byte*>(buf),
                                 reinterpret_cast<const std::byte*>(buf) + static_cast<std::size_t>(n));
                if (read_buf_.size() >= kReadSoftCap) {
                    return types::IoStatus::ok;
                }
                continue;
            }
            if (n == 0) {
                peer_closed_ = true;
                return types::IoStatus::closed;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return types::IoStatus::would_block;
            }
            return types::IoStatus::error;
        }
    }

    types::IoStatus Connections::on_writeable() {
        types::IoStatus st = flush();
        if (st == types::IoStatus::ok && peer_closed_) {
            ::shutdown(fd_, SHUT_WR);
            return types::IoStatus::closed;
        }
        return st;
    }

    types::IoStatus Connections::send(const char* data, std::size_t len) {
        if (len == 0) {
            return types::IoStatus::ok;
        }
        if (write_buf_.size() - write_pos_ + len > kWriteHighWater) {
            return types::IoStatus::error;
        }
        write_buf_.insert(write_buf_.end(),
                          reinterpret_cast<const std::byte*>(data),
                          reinterpret_cast<const std::byte*>(data) + len);
        return flush();
    }

    types::IoStatus Connections::flush() {
        while (write_pos_ < write_buf_.size()) {
            ssize_t n = ::send(fd_,
                               reinterpret_cast<const char*>(write_buf_.data()) + write_pos_,
                               write_buf_.size() - write_pos_,
                               MSG_NOSIGNAL);
            if (n > 0) {
                write_pos_ += static_cast<std::size_t>(n);
                continue;
            }
            if (n == 0) {
                return types::IoStatus::error;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                compact();
                return types::IoStatus::would_block;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                return types::IoStatus::closed;
            }
            return types::IoStatus::error;
        }
        write_buf_.clear();
        write_pos_ = 0;
        return types::IoStatus::ok;
    }

    void Connections::compact() noexcept {
        if (write_pos_ > kCompactThreshold && write_pos_ * 2 > write_buf_.size()) {
            write_buf_.erase(write_buf_.begin(), write_buf_.begin() + write_pos_);
            write_pos_ = 0;
        }
    }
} // namespace core