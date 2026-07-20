//
// Created by Neuroil on 2026/7/20.
//

#include "connections.h"
#include <unistd.h>

using namespace core;

Connections::Connections(int fd) noexcept : fd_(fd) {
    //
}

Connections::~Connections() {
    close(fd_);
}

types::IoStatus Connections::on_readable() {
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            read_buf_.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) return types::IoStatus::closed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return types::IoStatus::ok;
        return types::IoStatus::error;
    }
}

types::IoStatus Connections::on_writeable() {

}