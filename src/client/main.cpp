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

#include "common/message.h"
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

    types::RequestMsg r{};
    r.from_uid_ = "nana";
    r.to_uid_ = "hoshi";
    r.chat_type_ = types::ChatTypes::single;
    r.msg_type_ = types::MessageTypes::text;
    r.client_msg_id_ = "shizuka";
    r.content_ = "take a shower";

    FrameHeader f{};
    f.opcode_ = core::Opcode::request;
    f.status_ = core::Status::ok;

    //
    std::vector<std::byte> bytes;
    bytes.resize(512);

    core::RequestMessagePack r_msg_pack(f, r);

    uint32_t out_len = 0;
    core::Encoder::encode(r_msg_pack, bytes, out_len);
    send(client_fd, bytes.data(), bytes.size(), 0);

    char buffer[512] = {0};
    int len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (len > 0) {
        for (int i = 6; i < sizeof(buffer); ++i) {
            if (i % 150 == 0) std::cout << "\n";

            std::cout << buffer[i];
        }
        std::cout << std::endl;
    }

    // 5. 关闭连接
    close(client_fd);
    return 0;
}
