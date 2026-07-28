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

    const std::string msg =
        R"({"chat_type_":0,"client_msg_id_":"hash","content_":"Hello World!","from_uid_":"Neuroil","msg_type_":0,"to_uid_":"Evil"})";
    send(client_fd, msg.c_str(), msg.size(), 0);

    char buffer[1024] = {0};
    int len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (len > 0) {
        std::cout << "Server says: " << buffer << "\n";
    }

    // 5. 关闭连接
    close(client_fd);
    return 0;
}
