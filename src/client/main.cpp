//
// Created by Neuroil on 2026/7/28.
//

// 调试客户端：命令行可选的多帧测试模式（docs/logs/2026-07-31.md §4.8，S4），
// 覆盖手工验证 M1-M6，每个场景一次调用即可选中、无需改代码重编译：
//   M1（默认）单帧回显基线。
//   M2/M3 一次 send() 两帧、期望恰好两个回显（验证 drain 循环：粘包不丢帧、无 LT stall）。
//   M4 半帧 → 停顿 → 补齐，期望恰好一个回显（验证 incomplete 不消耗字节、无失步）。
//   M5 空 content_ 帧 + 合法帧，期望连接被关（验证 !msg.is_valid() 归 error 杀连接）。
//   M6 6 字节未知 opcode 帧，期望连接被杀（验证 is_known_opcode 拒绝）。
// 退出码：预期结果 → 0；异常（超时、回显缺失/多帧、连接未关）→ 非 0。

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "common/io_status.h"
#include "common/message.h"
#include "common/message_pack.h"
#include "common/encoder.h"

namespace {

constexpr int         kServerPort     = 7891;
constexpr int         kRecvTimeoutSec = 5;  // 第二帧 / 连接关闭必须在该时间内发生
constexpr std::size_t kM4PartialBody  = 8;  // M4 半帧：6 字节帧头 + 8 字节帧体

// 构造一条合法消息；content 为空时帧结构完整但 Message::is_valid() 为假（M5）。
Message make_message(const char* content, const char* msg_id) {
    Message r{};
    r.from_uid_      = "Remilia";
    r.to_uid_        = "Reimu";
    r.chat_type_     = types::ChatTypes::single;
    r.msg_type_      = types::MessageTypes::text;
    r.client_msg_id_ = msg_id;
    r.content_       = content;
    return r;
}

// 编码一帧，返回整帧字节。encode() 的 out_buf 可能大于 out_len（复用缓冲残留），
// 这里截断到 out_len，保证后续拼接不含残留字节。失败返回 false。
bool encode_frame(const Message& msg, types::Opcode opcode, types::Status status,
                  std::vector<std::byte>& out) {
    FrameHeader fh{};
    fh.opcode_ = opcode;
    fh.status_ = status;
    MessagePack pack(fh, msg);

    uint32_t out_len = 0;
    if (codec::Encoder::encode(pack, out, out_len) != types::IoStatus::ok) {
        return false;
    }
    out.resize(out_len);
    return true;
}

// 发送完整段（小帧一次系统调用即可发完，这里按返回值循环以防部分写）。
bool send_all(int fd, const std::byte* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool set_recv_timeout(int fd, int sec) {
    timeval tv{};
    tv.tv_sec = sec;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

// 从累积缓冲解析完整帧并打印（帧头字段 + 帧体字节，供人工比对）。
// 帧边界按「wire_size + 4 字节大端 body_len」判定；帧体未齐则保留字节继续等。
// count 为已解析帧总数（跨多次调用累计，帧序号据此续排）。
void drain_frames(std::vector<std::byte>& acc, int& count) {
    while (acc.size() >= FrameHeader::wire_size) {
        std::uint32_t body_len = 0;
        std::memcpy(&body_len, acc.data() + 2, sizeof(body_len));
        body_len = ntohl(body_len);

        const std::size_t frame_len = FrameHeader::wire_size + body_len;
        if (acc.size() < frame_len) {
            break; // 帧体未齐
        }

        std::cout << "  echo frame #" << (count + 1)
                  << ": opcode=" << static_cast<int>(acc[0])
                  << " status=" << static_cast<int>(acc[1])
                  << " body_len=" << body_len
                  << " body=";
        std::cout.write(
            reinterpret_cast<const char*>(acc.data() + FrameHeader::wire_size), body_len);
        std::cout << std::endl;

        acc.erase(acc.begin(), acc.begin() + static_cast<std::ptrdiff_t>(frame_len));
        ++count;
    }
}

// 带超时循环 recv，直到凑齐 want 个完整帧（或连接关闭 / 超时）。
// 返回：>= 0 为实际收到的完整帧数；-1 = 超时未凑齐；-2 = 连接关闭或 recv 错误。
int recv_frames(int fd, std::vector<std::byte>& acc, int want) {
    int count = 0; // 跨多次 recv 累计的完整帧总数
    for (;;) {
        drain_frames(acc, count);
        if (count >= want) {
            return count;
        }

        std::byte chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            acc.insert(acc.end(), chunk, chunk + n);
            continue;
        }
        if (n == 0) {
            std::cout << "  connection closed by peer (EOF), got " << count
                      << " of " << want << " frames\n";
            return -2;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cout << "  recv timeout (" << kRecvTimeoutSec << "s), got " << count
                      << " of " << want << " frames\n";
            return -1;
        }
        std::cout << "  recv error: " << std::strerror(errno) << "\n";
        return -2;
    }
}

// M5/M6：等服务端关闭连接，把 EOF / recv 错误当作预期结果。
// 返回 0 = 连接已关（预期）；1 = 超时仍连接（未预期，说明服务端没有杀连接）。
int recv_expect_close(int fd, std::vector<std::byte>& acc) {
    for (;;) {
        std::byte chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n == 0) {
            std::cout << "  connection closed by peer (EOF) -- expected\n";
            return 0;
        }
        if (n > 0) {
            std::cout << "  unexpected data before close: ";
            std::cout.write(reinterpret_cast<const char*>(chunk), n);
            std::cout << std::endl;
            continue; // 有数据先收完，仍然等 close
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cout << "  recv timeout: connection still open -- not expected\n";
            return 1;
        }
        // ECONNRESET 等错误也是"服务端关连接"的一种表现（close 未读净数据 → RST）
        std::cout << "  recv error: " << std::strerror(errno)
                  << " -- treated as expected close\n";
        return 0;
    }
}

void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--mode m1|m2|m3|m4|m5|m6]\n"
        << "  m1 (default) : 单帧回显基线\n"
        << "  m2/m3        : 一次 send() 发两个完整合法帧，期望恰好两个回显（验证 drain 无 LT stall / 粘包丢帧）\n"
        << "  m4           : 半帧（帧头 + 8B 帧体）→ 停顿 1.5s → 补齐，期望恰好一个回显（验证 incomplete 不消耗字节）\n"
        << "  m5           : 空 content_ 帧 + 合法帧一次 send()，期望连接被关\n"
        << "  m6           : 6 字节未知 opcode 帧，期望连接被杀\n"
        << "  也可写作 --mode=m2 或直接传 m2；退出码：预期结果 → 0，异常 → 非 0\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string mode = "m1";
    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "--mode") { // 缺值
            usage(argv[0]);
            return 2;
        }
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        mode = arg.rfind("--mode=", 0) == 0 ? arg.substr(7) : arg;
    } else if (argc == 3) {
        if (std::string(argv[1]) != "--mode") {
            usage(argv[0]);
            return 2;
        }
        mode = argv[2];
    } else if (argc > 3) {
        usage(argv[0]);
        return 2;
    }

    if (mode != "m1" && mode != "m2" && mode != "m3" && mode != "m4"
        && mode != "m5" && mode != "m6") {
        std::cerr << "Unknown mode: " << mode << "\n";
        usage(argv[0]);
        return 2;
    }

    std::cout << "mode=" << mode << std::endl;

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr) != 1) {
        std::cerr << "Failed to parse server address\n";
        close(client_fd);
        return 1;
    }
    server_addr.sin_port = htons(kServerPort);

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        std::cerr << "Failed to connect\n";
        close(client_fd);
        return 1;
    }
    std::cout << "Connected to server\n";

    if (!set_recv_timeout(client_fd, kRecvTimeoutSec)) {
        std::cerr << "Failed to set recv timeout\n";
        close(client_fd);
        return 1;
    }

    int result = 1;
    if (mode == "m1") {
        std::vector<std::byte> frame;
        if (!encode_frame(make_message("Cirno", "20011229"),
                          types::Opcode::request, types::Status::ok, frame)) {
            std::cerr << "Failed to encode message\n";
        } else {
            std::cout << "[m1] sending single frame (" << frame.size() << " bytes)\n";
            if (!send_all(client_fd, frame.data(), frame.size())) {
                std::cerr << "Failed to send\n";
            } else {
                std::vector<std::byte> acc;
                result = (recv_frames(client_fd, acc, 1) == 1) ? 0 : 1;
            }
        }
    } else if (mode == "m2" || mode == "m3") {
        std::vector<std::byte> f1, f2;
        if (!encode_frame(make_message("Cirno", "20011229"),
                          types::Opcode::request, types::Status::ok, f1) ||
            !encode_frame(make_message("Daiyousei", "20011230"),
                          types::Opcode::request, types::Status::ok, f2)) {
            std::cerr << "Failed to encode message\n";
        } else {
            std::vector<std::byte> blob = f1; // 两帧拼成一段，一次 send()
            blob.insert(blob.end(), f2.begin(), f2.end());
            std::cout << "[" << mode << "] sending two frames in one send(): "
                      << "frame1=" << f1.size() << "B frame2=" << f2.size()
                      << "B total=" << blob.size() << "B\n";
            if (!send_all(client_fd, blob.data(), blob.size())) {
                std::cerr << "Failed to send\n";
            } else {
                std::vector<std::byte> acc;
                const int got = recv_frames(client_fd, acc, 2);
                if (got == 2 && acc.empty()) {
                    std::cout << "[" << mode << "] exactly 2 complete echoes, no residual bytes "
                              << "(no dropped frame, no LT stall)\n";
                    result = 0;
                } else {
                    std::cout << "[" << mode << "] expected exactly 2 complete echoes, got " << got
                              << " frame(s)" << (acc.empty() ? "" : " + residual bytes") << "\n";
                }
            }
        }
    } else if (mode == "m4") {
        std::vector<std::byte> frame;
        if (!encode_frame(make_message("Cirno", "20011229"),
                          types::Opcode::request, types::Status::ok, frame)) {
            std::cerr << "Failed to encode message\n";
        } else {
            const std::size_t partial = FrameHeader::wire_size + kM4PartialBody;
            std::cout << "[m4] frame=" << frame.size() << "B; sending first " << partial
                      << " bytes (6B header + " << kM4PartialBody << "B body)\n";
            if (!send_all(client_fd, frame.data(), partial)) {
                std::cerr << "Failed to send partial frame\n";
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                std::cout << "[m4] after 1.5s, sending remaining "
                          << (frame.size() - partial) << " bytes\n";
                if (!send_all(client_fd, frame.data() + partial, frame.size() - partial)) {
                    std::cerr << "Failed to send frame remainder\n";
                } else {
                    std::vector<std::byte> acc;
                    const int got = recv_frames(client_fd, acc, 1);
                    if (got == 1) {
                        std::cout << "[m4] exactly one echo, stream in sync\n";
                        result = 0;
                    } else {
                        std::cout << "[m4] expected exactly 1 echo, got " << got
                                  << " (stream desynced?)\n";
                    }
                }
            }
        }
    } else if (mode == "m5") {
        std::vector<std::byte> bad, good;
        if (!encode_frame(make_message("", "20011229"),
                          types::Opcode::request, types::Status::ok, bad) ||
            !encode_frame(make_message("Cirno", "20011230"),
                          types::Opcode::request, types::Status::ok, good)) {
            std::cerr << "Failed to encode message\n";
        } else {
            std::vector<std::byte> blob = bad; // 空 content_ 帧在前，合法帧在后
            blob.insert(blob.end(), good.begin(), good.end());
            std::cout << "[m5] sending empty-content frame (" << bad.size()
                      << "B) + valid frame (" << good.size() << "B) in one send(), total "
                      << blob.size() << "B\n"
                      << "      empty-content frame passes frame-layer checks but "
                         "Message::is_valid() is false -> expect connection close\n";
            if (!send_all(client_fd, blob.data(), blob.size())) {
                std::cerr << "Failed to send\n";
            } else {
                std::vector<std::byte> acc;
                result = recv_expect_close(client_fd, acc);
            }
        }
    } else { // m6
        std::vector<std::byte> raw = {
            std::byte{0x07}, // opcode: 未知（枚举外线上字节，is_known_opcode 拒绝）
            std::byte{0x00}, // status: ok
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // body_len=0（大端）
        };
        std::cout << "[m6] sending 6-byte frame: opcode=0x07(unknown) status=ok body_len=0\n";
        if (!send_all(client_fd, raw.data(), raw.size())) {
            std::cerr << "Failed to send\n";
        } else {
            std::vector<std::byte> acc;
            result = recv_expect_close(client_fd, acc);
        }
    }

    close(client_fd);
    std::cout << (result == 0 ? "PASS" : "FAIL") << std::endl;
    return result;
}
