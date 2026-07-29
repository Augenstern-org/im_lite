//
// Created by Neuroil on 2026/7/28.
//

#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <vector>

#include "common/io_status.h"
#include "common/message.h"
#include "common/message_pack.h"
#include "server/core/encoder.h"

int main() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        std::cerr << "Failed to create socket\n";
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr);
    server_addr.sin_port = htons(7891);

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        std::cerr << "Failed to connect\n";
        return -1;
    }
    std::cout << "Connected to server\n";

    //

    Message r{};
    r.from_uid_      = "Remilia";
    r.to_uid_        = "Reimu";
    r.chat_type_     = types::ChatTypes::single;
    r.msg_type_      = types::MessageTypes::text;
    r.client_msg_id_ = "20011229";
    r.content_       = "Cirno";

    FrameHeader f{};
    f.opcode_ = core::Opcode::request;
    f.status_ = core::Status::ok;

    // Encoder 直接 memcpy 进 out_buf，不做越界检查
    // 缓冲按最大帧长分配
    std::vector<std::byte> bytes;
    bytes.resize(FrameHeader::wire_size + max_message_body_length);

    MessagePack r_msg_pack(f, r);

    uint32_t out_len = 0;
    if (core::Encoder::encode(r_msg_pack, bytes, out_len) != types::IoStatus::ok) {
        std::cerr << "Failed to encode message\n";
        close(client_fd);
        return -1;
    }

    send(client_fd, bytes.data(), out_len, 0);

    char buffer[512] = {0};

    int len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (len > static_cast<int>(FrameHeader::wire_size)) {
        std::cout.write(buffer + FrameHeader::wire_size, len - FrameHeader::wire_size);
        std::cout << std::endl;
    }

    close(client_fd);
    return 0;
}
