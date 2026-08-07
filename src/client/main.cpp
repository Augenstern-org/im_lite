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
//
// M1-M7 都是单连接场景，观测不到「跨 fd 转发」这条路径。M8-M10 是双进程场景：
// 两个客户端进程各自以不同 uid 登录（服务端演示用户表 src/server/main.cpp:21-24 只有
// alice / bob 两个），由 M10 向对端 uid 发 request、M8/M9 作为对端收帧，
// 从而让「发送方 fd」与「转发目标 fd」在一次 request 里分离，可分别观测生死。
//   M8  接收方：登录后持续收帧并打印，看转发是否真的落到对端进程。
//   M9  接收方（停读变体）：登录后保持连接但永不 recv。服务端向本连接的写缓冲
//       只进不出，越过 connections.cpp 的 kWriteHighWater(1MiB) 后 send() 返回 error
//       —— 这是唯一可从客户端触发的「写目标失败」路径。
//   M10 发送方：向对端 uid 连发 request（条数与帧体大小均由命令行给出），
//       边发边排空自身入站帧，发完后驻留观察本连接是否被服务端一并杀掉。

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
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

// M8-M10 用
constexpr std::size_t kNoBodyCap        = static_cast<std::size_t>(-1); // 不截断帧体打印
constexpr std::size_t kBriefBodyBytes   = 128; // 双进程场景帧体可达 64KiB，只打印前若干字节
constexpr int         kTickSec          = 1;   // 接收方每次 recv 的超时片，用于逼近驻留时限
constexpr int         kHoldSecDefault   = 60;  // 接收方默认驻留秒数（等人工启动发送方）
constexpr int         kSenderHoldSec    = 10;  // 发送方发完后默认驻留秒数（观察是否被杀）
constexpr std::size_t kSenderCount      = 1;   // 发送方默认发帧条数
constexpr std::size_t kSenderContentLen = 64;  // 发送方默认 content_ 字节数
constexpr int         kPollTimeoutMs    = 5000; // 发送侧不可写时单次等待上限
constexpr std::size_t kProgressEvery    = 16;  // 发送方每若干帧打印一次进度

// 防误输护栏：基准场景单次 1M 条已远超需要，更大的 count 几乎必是手误 —— count 只驱动发送
// 循环（无 OOM），但会让本次运行时长任意拉长，纯属自伤。
constexpr std::size_t kMaxSenderCount = 1'000'000;
// 入站累积缓冲上限：防恶意对端声明超大 body_len 后慢速灌数据（帧体永不凑齐，acc 只进不出、
// 无界增长）。1MiB 对齐服务端 kWriteHighWater 语义；合法帧最大 6+65530=65536B，此处是其
// 16 倍，不会误伤正常流量。
constexpr std::size_t kMaxAccBytes = 1 * 1024 * 1024;

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

// M1-M6 以已登录连接为前提（登录协议落地后，未登录连接发 request/ack 会被服务端拒绝）。
// 连接建立后先发 login 帧并消耗一帧响应；返回 false 表示登录帧发送失败或未收到响应。
// uid / credential 由调用方给出：M1-M6 固定用演示表里的 alice，M8-M10 由命令行指定，
// 两个进程才能以不同身份登录同一台服务端。
// out_status 回写响应帧头的 status 字节（0 = ok）；M1-M6 不看它，M8-M10 据此拒绝带病继续。
bool pre_login(int fd, const std::string& uid, const std::string& credential,
               std::uint8_t& out_status) {
    out_status = static_cast<std::uint8_t>(types::Status::_init_);

    Message msg;
    msg.chat_type_     = types::ChatTypes::single;
    msg.msg_type_      = types::MessageTypes::text;
    msg.from_uid_      = uid;
    msg.to_uid_        = uid;
    msg.client_msg_id_ = "pre-login";
    msg.content_       = credential; // 与服务端 main.cpp 演示用户表一致

    std::vector<std::byte> frame;
    if (!encode_frame(msg, types::Opcode::login, types::Status::ok, frame)) {
        std::cerr << "[pre-login] failed to encode login frame\n";
        return false;
    }
    if (!send_all(fd, frame.data(), frame.size())) {
        std::cerr << "[pre-login] failed to send login frame\n";
        return false;
    }

    std::byte chunk[4096];
    const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
    if (n < static_cast<ssize_t>(FrameHeader::wire_size)) {
        std::cerr << "[pre-login] no login response (closed / timeout)\n";
        return false;
    }
    out_status = static_cast<std::uint8_t>(chunk[1]);
    std::cout << "[pre-login] login response: opcode=" << static_cast<int>(chunk[0])
              << " status=" << static_cast<int>(chunk[1]) << "\n";
    return true;
}

// M8-M10 的登录入口：登录必须成功，否则后续 request 会被服务端当未认证帧丢弃，
// 整个双进程场景失去意义 —— 这里直接判死，不带病继续。
bool login_as(int fd, const std::string& uid, const std::string& credential) {
    std::uint8_t status = 0;
    if (!pre_login(fd, uid, credential, status)) {
        return false;
    }
    if (status != static_cast<std::uint8_t>(types::Status::ok)) {
        std::cerr << "[login] uid=" << uid << " rejected (status=" << static_cast<int>(status)
                  << ")\n";
        return false;
    }
    std::cout << "[login] uid=" << uid << " logged in\n";
    return true;
}

// 从累积缓冲解析完整帧并打印（帧头字段 + 帧体字节，供人工比对）。
// 帧边界按「wire_size + 4 字节大端 body_len」判定；帧体未齐则保留字节继续等。
// count 为已解析帧总数（跨多次调用累计，帧序号据此续排）。
// label 是打印用的帧别（M1-M6 是自身回显，M8-M10 收的是服务端转发/响应）。
// body_cap 限制帧体打印字节数，kNoBodyCap 表示原样全打（M1-M6 帧体只有百余字节）。
void drain_frames(std::vector<std::byte>& acc, int& count, const char* label,
                  std::size_t body_cap) {
    while (acc.size() >= FrameHeader::wire_size) {
        std::uint32_t body_len = 0;
        std::memcpy(&body_len, acc.data() + 2, sizeof(body_len));
        body_len = ntohl(body_len);

        const std::size_t frame_len = FrameHeader::wire_size + body_len;
        if (acc.size() < frame_len) {
            break; // 帧体未齐
        }

        const std::size_t print_len = body_len < body_cap ? body_len : body_cap;
        std::cout << "  " << label << " frame #" << (count + 1)
                  << ": opcode=" << static_cast<int>(acc[0])
                  << " status=" << static_cast<int>(acc[1])
                  << " body_len=" << body_len
                  << " body=";
        std::cout.write(
            reinterpret_cast<const char*>(acc.data() + FrameHeader::wire_size), print_len);
        if (print_len < body_len) {
            std::cout << "...(+" << (body_len - print_len) << "B)";
        }
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
        drain_frames(acc, count, "echo", kNoBodyCap);
        if (count >= want) {
            return count;
        }

        std::byte chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            acc.insert(acc.end(), chunk, chunk + n);
            // 合法流量下 acc 峰值 = 一个未凑齐的帧残余（< 65536B）+ 本次 4096B；超过
            // kMaxAccBytes 只可能来自恶意对端声明超大 body_len 后慢速灌数据（帧永不凑齐）。
            if (acc.size() > kMaxAccBytes) {
                std::cout << "  inbound buffer exceeded " << kMaxAccBytes
                          << "B (peer declared oversized body_len?) -- treating as connection error\n";
                return -2;
            }
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
// acc 只为与 recv_frames 保持同一组调用签名而保留：这里等的是 close，
// 收到的残余数据直接打印、不入缓冲区。
int recv_expect_close(int fd, [[maybe_unused]] std::vector<std::byte>& acc) {
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

// 十进制无符号命令行参数解析。空串、含非数字字符、溢出一律判非法（返回 false，不改 out）。
bool parse_uint(const std::string& s, std::size_t& out) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    try {
        out = static_cast<std::size_t>(std::stoull(s));
    } catch (...) { // out_of_range
        return false;
    }
    return true;
}

// M10 帧体：前缀带序号便于在接收端逐帧对号，其余用 'A' 填到指定长度
// （'A' 不触发 JSON 转义，帧体字节数 ≈ 固定开销 + content_bytes）。
std::string make_padded_content(std::size_t index, std::size_t content_bytes) {
    std::string s = "s4a-" + std::to_string(index);
    if (s.size() < content_bytes) {
        s.append(content_bytes - s.size(), 'A');
    }
    return s;
}
// 非阻塞排空自身入站字节并打印已凑齐的帧。
// 返回 false = 对端已关闭或 recv 出错（此时残留字节先解完再报）；true = 已读到 EAGAIN。
bool pump_inbound(int fd, std::vector<std::byte>& acc, int& in_count) {
    for (;;) {
        std::byte     chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            acc.insert(acc.end(), chunk, chunk + n);
            drain_frames(acc, in_count, "recv", kBriefBodyBytes); // 每轮就地解帧，acc 不无界增长
            // 恶意对端声明超大 body_len 后慢速灌数据时帧永不凑齐，解帧后仍有残留 → 无界增长。
            if (acc.size() > kMaxAccBytes) {
                std::cout << "  inbound buffer exceeded " << kMaxAccBytes
                          << "B (peer declared oversized body_len?) -- treating as connection error\n";
                return false;
            }
            continue;
        }
        if (n == 0) {
            drain_frames(acc, in_count, "recv", kBriefBodyBytes);
            std::cout << "  connection closed by peer (EOF) after " << in_count
                      << " inbound frame(s)\n";
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        std::cout << "  recv error: " << std::strerror(errno) << "\n";
        return false;
    }
}

// M8 / M9 接收方。keep_reading 为真时持续收帧并打印；为假时登录后一次也不 recv
// （停读变体），让服务端向本连接的写缓冲堆到 kWriteHighWater 之上。
// 返回 0 = 驻留期满、连接仍在（M8 还要求至少收到一帧）；1 = 期间被对端关闭或出错。
int run_receiver(int fd, bool keep_reading, int hold_sec) {
    // 驻留期以 kTickSec 为片：阻塞 recv 超时即为一片，无需额外定时器
    if (keep_reading && !set_recv_timeout(fd, kTickSec)) {
        std::cerr << "Failed to set recv tick timeout\n";
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(hold_sec);
    std::vector<std::byte> acc;
    int                    count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        if (!keep_reading) {
            // 关键：不 recv。内核接收缓冲填满后通告零窗口，服务端 write_buf_ 随之堆积。
            std::this_thread::sleep_for(std::chrono::seconds(kTickSec));
            continue;
        }

        std::byte     chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            acc.insert(acc.end(), chunk, chunk + n);
            drain_frames(acc, count, "recv", kBriefBodyBytes);
            // 与 pump_inbound 同一防护：恶意对端声明超大 body_len 后慢速灌数据，acc 只进不出。
            if (acc.size() > kMaxAccBytes) {
                std::cout << "  inbound buffer exceeded " << kMaxAccBytes
                          << "B (peer declared oversized body_len?) -- treating as connection error\n";
                return 1;
            }
            continue;
        }
        if (n == 0) {
            std::cout << "  connection closed by peer (EOF) after " << count
                      << " inbound frame(s)\n";
            return 1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue; // 一个空闲时间片，继续等
        }
        std::cout << "  recv error: " << std::strerror(errno) << "\n";
        return 1;
    }

    if (!keep_reading) {
        std::cout << "  held " << hold_sec << "s without reading a single byte; still connected\n";
        return 0;
    }
    std::cout << "  held " << hold_sec << "s, received " << count
              << " frame(s), still connected\n";
    return count > 0 ? 0 : 1;
}

// M10 发送方。向 peer_uid 连发 count 条 request，边发边排空自身入站帧
// —— 不排空的话自身响应帧会先把服务端到本连接的写缓冲顶爆，观测点就从
// 「写目标失败」污染成「写自己失败」，两者的处置差异正是要观测的东西。
// 返回 0 = 全部发完且驻留期内本连接仍存活；1 = 中途/驻留期内被服务端关闭或出错。
int run_sender(int fd, const std::string& uid, const std::string& peer_uid,
               std::size_t count, std::size_t content_bytes, int hold_sec) {
    // 转成非阻塞：单线程里交替 send / recv 必须靠 EAGAIN 让出，不能被阻塞 send 卡死
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set non-blocking mode\n";
        return 1;
    }

    std::vector<std::byte> acc;
    int                    in_count    = 0;
    std::size_t            sent_frames = 0;
    std::size_t            sent_bytes  = 0;
    bool                   closed      = false;

    for (std::size_t i = 0; i < count && !closed; ++i) {
        Message msg;
        msg.chat_type_     = types::ChatTypes::single;
        msg.msg_type_      = types::MessageTypes::text;
        msg.from_uid_      = uid;
        msg.to_uid_        = peer_uid;
        msg.client_msg_id_ = "s4a-" + std::to_string(i);

        // 预检：content_bytes 超限时在 make_padded_content 之前拒绝——
        // 否则先分配 ~9.31GiB 再检查，WSL2 内存上限下先 OOM 杀死进程（无声消失）。
        if (content_bytes > max_message_body_length) {
            std::cerr << "[m10] failed to encode frame #" << (i + 1) << ": content_bytes="
                      << content_bytes << " likely pushes the body past max_message_body_length="
                      << max_message_body_length << "\n";
            return 1;
        }
        msg.content_ = make_padded_content(i, content_bytes);

        std::vector<std::byte> frame;
        if (!encode_frame(msg, types::Opcode::request, types::Status::ok, frame)) {
            std::cerr << "[m10] failed to encode frame #" << (i + 1) << ": content_bytes="
                      << content_bytes << " likely pushes the body past max_message_body_length="
                      << max_message_body_length << "\n";
            return 1;
        }
        if (i == 0) {
            std::cout << "[m10] frame size=" << frame.size() << "B (content_bytes="
                      << content_bytes << "), sending " << count << " request(s) to uid="
                      << peer_uid << "\n";
        }

        std::size_t off = 0;
        while (off < frame.size()) {
            // MSG_NOSIGNAL：服务端若已关掉本连接，写入必须以 EPIPE 返回而不是 SIGPIPE 打死进程
            const ssize_t n = send(fd, frame.data() + off, frame.size() - off, MSG_NOSIGNAL);
            if (n > 0) {
                off += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!pump_inbound(fd, acc, in_count)) { // 先排空入站，给服务端腾出读进度
                    closed = true;
                    break;
                }
                pollfd   pfd{};
                pfd.fd     = fd;
                pfd.events = POLLIN | POLLOUT;
                const int pr = poll(&pfd, 1, kPollTimeoutMs);
                if (pr == 0) {
                    std::cout << "  [m10] send stalled " << (kPollTimeoutMs / 1000)
                              << "s at frame #" << (i + 1) << " (" << off << "/" << frame.size()
                              << "B)\n";
                } else if (pr < 0 && errno != EINTR) {
                    std::cout << "  [m10] poll error: " << std::strerror(errno) << "\n";
                    return 1;
                }
                continue;
            }
            std::cout << "  [m10] send error at frame #" << (i + 1) << " (" << off << "/"
                      << frame.size() << "B): " << std::strerror(errno)
                      << " -- server closed this connection\n";
            closed = true;
            break;
        }
        if (closed) {
            break;
        }

        ++sent_frames;
        sent_bytes += frame.size();
        if (!pump_inbound(fd, acc, in_count)) {
            closed = true;
            break;
        }
        if (sent_frames % kProgressEvery == 0) {
            std::cout << "  [m10] progress: sent " << sent_frames << "/" << count << " frame(s), "
                      << sent_bytes << "B out, " << in_count << " frame(s) in\n";
        }
    }

    std::cout << "[m10] send phase done: sent " << sent_frames << "/" << count << " frame(s), "
              << sent_bytes << "B out, " << in_count << " frame(s) in\n";
    if (closed) {
        std::cout << "[m10] sender connection is GONE\n";
        return 1;
    }

    // 驻留观察：跨 fd 写失败在服务端是同一轮 epoll 里的决策，若本连接被牵连关闭，
    // EOF 会紧随其后到达。驻留期满仍在线，才说明发送方活了下来。
    std::cout << "[m10] holding " << hold_sec << "s to observe whether this connection survives\n";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(hold_sec);
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, kTickSec * 1000);
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::cout << "  [m10] poll error: " << std::strerror(errno) << "\n";
            return 1;
        }
        if (pr > 0 && !pump_inbound(fd, acc, in_count)) {
            std::cout << "[m10] sender connection is GONE (closed during hold)\n";
            return 1;
        }
    }
    std::cout << "[m10] sender still connected after " << hold_sec << "s, total " << in_count
              << " inbound frame(s)\n";
    return 0;
}

void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--mode m1|m2|m3|m4|m5|m6|m7|m8|m9|m10 [args...]]\n"
        << "  m1 (default) : 单帧回显基线\n"
        << "  m2/m3        : 一次 send() 发两个完整合法帧，期望恰好两个回显（验证 drain 无 LT stall / 粘包丢帧）\n"
        << "  m4           : 半帧（帧头 + 8B 帧体）→ 停顿 1.5s → 补齐，期望恰好一个回显（验证 incomplete 不消耗字节）\n"
        << "  m5           : 空 content_ 帧 + 合法帧一次 send()，期望连接被关\n"
        << "  m6           : 6 字节未知 opcode 帧，期望连接被杀\n"
        << "  m7 <uid> <credential> : 登录：发 login 帧，按响应帧头 status 判定（ok -> 0，fail -> 1）\n"
        << "  m8 <uid> <credential> [hold_sec] : 接收方：登录后持续收帧并打印，默认驻留 "
        << kHoldSecDefault << "s（收到 >=1 帧且未被关 -> 0）\n"
        << "  m9 <uid> <credential> [hold_sec] : 接收方（停读变体）：登录后永不 recv，只保持连接，默认驻留 "
        << kHoldSecDefault << "s\n"
        << "  m10 <uid> <credential> <peer_uid> [count] [content_bytes] [hold_sec] :\n"
        << "        发送方：向 peer_uid 连发 count 条 request（默认 " << kSenderCount
        << " 条 / content_bytes=" << kSenderContentLen << "），边发边收，\n"
        << "        发完驻留 hold_sec（默认 " << kSenderHoldSec
        << "s）观察本连接是否被牵连关闭（存活 -> 0，被关 -> 1）\n"
        << "  双进程用法（uid 取服务端演示表 alice/bob）：先起 m8 或 m9 作接收方，再起 m10 作发送方。\n"
        << "  m9 + 足量的 m10 可把目标连接的服务端写缓冲顶过 1MiB 高水位，触发「写目标失败」路径。\n"
        << "  也可写作 --mode=m7 <uid> <credential> 或直接传 m7；退出码：预期结果 → 0，异常 → 非 0\n";
}

} // namespace
int main(int argc, char* argv[]) {
    std::string mode = "m1";
    // 模式后的位置参数原样收集：m7 只用两个，m10 最多用四个，语义由各模式分支自行解读。
    std::vector<std::string> args;

    if (argc >= 2) {
        const std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        int first = 0; // 位置参数在 argv 中的起点，0 表示没有
        if (arg == "--mode") {                     // --mode m7 uid credential
            if (argc < 3) {
                usage(argv[0]);
                return 2;
            }
            mode  = argv[2];
            first = 3;
        } else if (arg.rfind("--mode=", 0) == 0) { // --mode=m7 uid credential
            mode  = arg.substr(7);
            first = 2;
        } else {                                   // m7 uid credential
            mode  = arg;
            first = 2;
        }
        for (int i = first; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
    }

    if (mode != "m1" && mode != "m2" && mode != "m3" && mode != "m4"
        && mode != "m5" && mode != "m6" && mode != "m7"
        && mode != "m8" && mode != "m9" && mode != "m10") {
        std::cerr << "Unknown mode: " << mode << "\n";
        usage(argv[0]);
        return 2;
    }

    // m7-m10 都以「显式身份」为前提；m10 还要一个投递目标 uid。
    const bool needs_identity = (mode == "m7" || mode == "m8" || mode == "m9" || mode == "m10");
    std::string uid, credential, peer_uid;
    if (needs_identity) {
        if (args.size() < 2 || args[0].empty() || args[1].empty()) {
            std::cerr << mode << " requires <uid> <credential>\n";
            usage(argv[0]);
            return 2;
        }
        uid        = args[0];
        credential = args[1];
    }

    // m8 / m9 的 [hold_sec]，m10 的 [count] [content_bytes] [hold_sec]
    std::size_t hold_sec      = kHoldSecDefault;
    std::size_t sender_count  = kSenderCount;
    std::size_t content_bytes = kSenderContentLen;
    if (mode == "m8" || mode == "m9") {
        if (args.size() > 2 && !parse_uint(args[2], hold_sec)) {
            std::cerr << mode << ": hold_sec must be a non-negative integer\n";
            return 2;
        }
    } else if (mode == "m10") {
        if (args.size() < 3 || args[2].empty()) {
            std::cerr << "m10 requires <uid> <credential> <peer_uid>\n";
            usage(argv[0]);
            return 2;
        }
        peer_uid = args[2];
        hold_sec = static_cast<std::size_t>(kSenderHoldSec);
        if ((args.size() > 3 && !parse_uint(args[3], sender_count))
            || (args.size() > 4 && !parse_uint(args[4], content_bytes))
            || (args.size() > 5 && !parse_uint(args[5], hold_sec))) {
            std::cerr << "m10: count / content_bytes / hold_sec must be non-negative integers\n";
            return 2;
        }
        if (sender_count == 0 || content_bytes == 0) {
            std::cerr << "m10: count and content_bytes must be >= 1\n";
            return 2;
        }
        if (sender_count > kMaxSenderCount) {
            std::cerr << "m10: count too large (max " << kMaxSenderCount << " per run)\n";
            return 1;
        }
    }
    // 驻留秒数进 std::chrono::seconds(int)，先钉在 int 值域内
    if (hold_sec > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::cerr << mode << ": hold_sec too large\n";
        return 2;
    }
    const int hold_seconds = static_cast<int>(hold_sec);

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


    // M1-M6 以已登录连接为前提，固定用演示表里的 alice；
    // m7 本身是登录场景，m8-m10 在各自分支里按命令行身份登录，都不走这里。
    if (!needs_identity) {
        std::uint8_t pre_status = 0;
        if (!pre_login(client_fd, "alice", "alice123", pre_status)) {
            std::cerr << "pre-login failed, aborting\n";
            close(client_fd);
            return 1;
        }
    }
    int result = 1;
    if (mode == "m1") {
        std::vector<std::byte> frame;
        if (!encode_frame(make_message("Cirno", "20020811"),
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
        if (!encode_frame(make_message("Cirno", "20020811"),
                          types::Opcode::request, types::Status::ok, f1) ||
            !encode_frame(make_message("Baka", "20260910"),
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
        if (!encode_frame(make_message("Cirno", "20020811"),
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
        if (!encode_frame(make_message("", "20020811"),
                          types::Opcode::request, types::Status::ok, bad) ||
            !encode_frame(make_message("Cirno", "20260910"),
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
    } else if (mode == "m6") {
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
    } else if (mode == "m7") { // m7 登录
        Message msg;
        msg.chat_type_     = types::ChatTypes::single;
        msg.msg_type_      = types::MessageTypes::text;
        msg.from_uid_      = uid;
        msg.to_uid_        = uid; // 登录帧 to_uid_ 无业务语义，填自身保持 is_valid()
        msg.client_msg_id_ = "login-001";
        msg.content_       = credential;

        std::vector<std::byte> frame;
        if (!encode_frame(msg, types::Opcode::login, types::Status::ok, frame)) {
            std::cerr << "Failed to encode message\n";
        } else {
            std::cout << "[m7] sending login frame: uid=" << uid
                      << " credential=" << credential << " (" << frame.size() << " bytes)\n";
            if (!send_all(client_fd, frame.data(), frame.size())) {
                std::cerr << "Failed to send\n";
            } else {
                std::byte chunk[4096];
                const ssize_t n = recv(client_fd, chunk, sizeof(chunk), 0);
                if (n >= static_cast<ssize_t>(FrameHeader::wire_size)) {
                    const auto op = static_cast<std::uint8_t>(chunk[0]);
                    const auto st = static_cast<std::uint8_t>(chunk[1]);
                    std::uint32_t body_len = 0;
                    std::memcpy(&body_len, chunk + 2, sizeof(body_len));
                    body_len = ntohl(body_len);
                    // 只打印已实际到达的帧体字节，防声明长度超出本次 recv 越界读。
                    const std::size_t got_body =
                        static_cast<std::size_t>(n) - FrameHeader::wire_size;
                    const std::size_t print_body = body_len < got_body ? body_len : got_body;
                    std::cout << "[m7] login response: opcode=" << static_cast<int>(op)
                              << " status=" << static_cast<int>(st)
                              << " body_len=" << body_len << " body=";
                    std::cout.write(
                        reinterpret_cast<const char*>(chunk + FrameHeader::wire_size), print_body);
                    std::cout << std::endl;
                    if (op == static_cast<std::uint8_t>(types::Opcode::response)) {
                        result = (st == static_cast<std::uint8_t>(types::Status::ok)) ? 0 : 1;
                    }
                } else if (n == 0) {
                    std::cout << "[m7] connection closed by peer before login response\n";
                } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    std::cout << "[m7] recv timeout waiting for login response\n";
                } else {
                    std::cout << "[m7] recv error: " << std::strerror(errno) << "\n";
                }
            }
        }
    } else if (mode == "m8" || mode == "m9") {
        // 双进程场景的接收侧：m8 正常读，m9 登录后停读（用于把服务端写缓冲顶过高水位）
        if (login_as(client_fd, uid, credential)) {
            const bool keep_reading = (mode == "m8");
            std::cout << "[" << mode << "] receiver uid=" << uid
                      << (keep_reading ? " reading" : " NOT reading (stalled)")
                      << ", holding " << hold_seconds << "s\n";
            result = run_receiver(client_fd, keep_reading, hold_seconds);
        }
    } else if (mode == "m10") {
        // 双进程场景的发送侧
        if (login_as(client_fd, uid, credential)) {
            result = run_sender(client_fd, uid, peer_uid, sender_count, content_bytes,
                                hold_seconds);
        }
    }

    close(client_fd);
    std::cout << (result == 0 ? "PASS" : "FAIL") << std::endl;
    return result;
}
