//
// Created by Neuroil on 2026/7/27.
//
// 测试公共设施：断言宏 + 手工构帧工具。
//

#ifndef COM_LITE_TEST_SUPPORT_H
#define COM_LITE_TEST_SUPPORT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// 不受 NDEBUG 影响的断言：Release 构建下同样生效。
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n",           \
                         #cond, __FILE__, __LINE__);                         \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

// 手工构帧：大端长度由移位写出，刻意不用 htonl、不 include <netinet/in.h>、
// 不触碰 Encoder —— 否则黄金向量会与被测代码共享同一个字节序 bug。
// declared_len 独立于 body.size()，以便构造"头部声明长度与实际载荷不符"的畸形帧。
inline std::vector<std::byte> make_frame(std::uint8_t       opcode,
                                         std::uint8_t       status,
                                         std::uint32_t      declared_len,
                                         const std::string& body) {
    std::vector<std::byte> f;
    f.reserve(6 + body.size());

    f.push_back(static_cast<std::byte>(opcode));
    f.push_back(static_cast<std::byte>(status));

    // 网络字节序（大端）：显式移位，不依赖任何主机字节序转换函数。
    f.push_back(static_cast<std::byte>((declared_len >> 24) & 0xFFu));
    f.push_back(static_cast<std::byte>((declared_len >> 16) & 0xFFu));
    f.push_back(static_cast<std::byte>((declared_len >> 8) & 0xFFu));
    f.push_back(static_cast<std::byte>(declared_len & 0xFFu));

    for (char c : body) {
        f.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return f;
}

// 便捷重载：declared_len 取 body.size()，用于构造格式良好的帧。
inline std::vector<std::byte> make_frame(std::uint8_t       opcode,
                                         std::uint8_t       status,
                                         const std::string& body) {
    return make_frame(opcode, status, static_cast<std::uint32_t>(body.size()), body);
}

// 从字节缓冲取出 [off, off + n) 作为 std::string。
inline std::string body_of(const std::vector<std::byte>& buf, std::size_t off, std::size_t n) {
    CHECK(off <= buf.size());
    CHECK(n <= buf.size() - off);
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(static_cast<char>(static_cast<unsigned char>(buf[off + i])));
    }
    return s;
}

// 失败诊断用：前 n 字节的十六进制转储。
inline std::string hex_dump(const std::vector<std::byte>& buf, std::size_t n) {
    if (n > buf.size()) n = buf.size();
    std::string out;
    out.reserve(n * 3);
    char tmp[4];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(tmp, sizeof(tmp), "%02X ", static_cast<unsigned>(static_cast<unsigned char>(buf[i])));
        out += tmp;
    }
    return out;
}

#endif //COM_LITE_TEST_SUPPORT_H
