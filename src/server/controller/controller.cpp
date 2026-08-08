//
// Created by Neuroil on 2026/7/16.
//

#include "server/controller/controller.h"

#include <iostream>
#include <utility>
#include <vector>

#include "common/binary_code.h"
#include "common/io_status.h"

namespace controller {
    void Controller::process(
        int                                       fd,
        const MessagePack&                        rmp,
        std::vector<std::pair<MessagePack, int>>& out_queue
    ) {
        // 入口校验（docs/architecture.md §2.1）。message_handler_ 只在 decode() 返回 ok 时被调用
        // （core.cpp drain 循环），而 ok 蕴含 msg.is_valid() 已过且帧头两枚举已知 —— 本检查是
        // 文档化的死代码：Controller::process 是公有方法，跨层防御便宜，兜底保留
        // （docs/logs/2026-07-31.md §4.6）。
        if (!rmp.is_valid()) {
            return;
        }

        std::vector<MessagePack> assembled;
        types::IoStatus          st = types::IoStatus::ok;

        if (rmp.fh_.opcode_ == types::Opcode::login) {
            // 登录：校验凭证，成功则把 fd 绑定到 uid
            const Message& m  = rmp.msg_;
            bool           ok = auth(m.from_uid_, m.content_);
            if (ok) {
                // 绑定的三态由 UsersGroup 内部分类 —— 这里不做 has_fd 之类的前置探测：
                // 双表不变式只归同时持有两表的 UsersGroup 所有，在外层探测既非原子，
                // 也会在下一个调用点重写一遍
                switch (register_user(m.from_uid_, fd)) {
                    // 新绑定，或同一 fd 重复登录同一 uid（幂等）—— 都算登录成功
                    case coordination::RegisterResult::Registered:
                    case coordination::RegisterResult::AlreadyBound: break;
                    // 该 fd 已属于另一个 uid：拒绝改绑，原绑定保持不变，连接保留可重试
                    case coordination::RegisterResult::FdConflict:
                        std::cout << "[controller] reject login as \"" << m.from_uid_ << "\" on fd " << fd
                                  << ": already bound to another uid\n";
                        ok = false;
                        break;
                }
                // 有意不写 default:，新增枚举值时由 -Wswitch-enum 提醒此处需同步扩展
                // （同 common/binary_code.h:42 的既有做法）
            }
            st = assembler_.assemble_login_result(rmp, ok ? types::Status::ok : types::Status::fail, assembled);
        } else {
            // 未登录连接只能发登录帧，其余一律拒绝（协议违规客户端不响应）。
            if (!users_group_.has_fd(fd)) {
                std::cout << "[controller] reject frame (opcode=" << static_cast<int>(rmp.fh_.opcode_)
                          << ") from unauthenticated fd " << fd << "\n";
                return;
            }

            // Opcode 判断消息类型：
            //      req -> 返回 res（转发待跨 fd 写入落地）
            //      ack -> 应答 ack
            if (rmp.fh_.opcode_ == types::Opcode::ack) {
                st = assembler_.assemble_ack(rmp, assembled);
            } else if (rmp.fh_.opcode_ == types::Opcode::request) {
                // int        to_fd  = -1;
                // const bool online = query(rmp.msg_.to_uid_, to_fd);

                // 先查路由、再装配响应：响应的 status 取决于目标是否在线，顺序不可颠倒
                const std::vector<int>* fds = nullptr;
                const bool online = query_all(rmp.msg_.to_uid_, fds);

                // 目标在线 = ok（已入转发队列）；不在线 = fail（无处投递，发送方必须知情）。
                // 注意这里只表达「路由结果」，不表达「投递保证」：目标 fd 写失败 / 积压超高水位
                // 由 core 层就地记账并打日志，不回传发送方（离线消息不在范围内，见 TODO.md:159-160）。
                st = assembler_.assemble_response(rmp, online ? types::Status::ok : types::Status::fail, assembled);

                // 转发。必须先于尾部 assembled 回显入队，out_queue 的这一顺序是被测试约束的。
                if (online) {
                    for (auto to_fd : *fds) {
                        out_queue.emplace_back(rmp, to_fd);
                    }
                } else {
                    std::cout << "[controller] drop request from \"" << rmp.msg_.from_uid_ << "\" (fd " << fd
                              << ") to \"" << rmp.msg_.to_uid_ << "\": peer not online\n";
                }
            } else {
                // 只有服务端才会返回 res。
                // 因此不接收 response，当前静默丢弃
                return;
            }
        }

        if (st != types::IoStatus::ok) {
            return;
        }

        // 目前所有出站帧的目标 fd 恒等于入站 fd。
        for (MessagePack& mp : assembled) {
            out_queue.emplace_back(std::move(mp), fd);
        }
    }

    bool Controller::query(const std::string& uid, int& fd) const {
        return users_group_.query(uid, fd);
    }

    // 必须传入指针，成功返回只读向量地址，失败返回空指针
    bool Controller::query_all(const std::string& uid, const std::vector<int>*& fds) const {
        return users_group_.query_all(uid, fds);
    }

    bool Controller::auth(const std::string& uid, const std::string& credential) const {
        // 静态用户表查证。真实认证（数据库 / 外部鉴权）落地前先查表比对。
        auto it = users_.find(uid);
        return it != users_.end() && it->second == credential;
    }

    coordination::RegisterResult Controller::register_user(const std::string& uid, int fd) {
        return users_group_.register_user(uid, fd);
    }

    bool Controller::delete_fd(int fd) {
        return users_group_.delete_fd(fd);
    }



} // controller
