//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_CONTROLLER_H
#define COM_LITE_CONTROLLER_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/message_pack.h"
#include "server/coordination/assembler.h"
#include "server/coordination/users_group.h"

namespace controller {
    class Controller {
    public:
        // uid -> 登录凭证（密码 / token）。演示用的静态用户表，真实认证落地前先查表比对。
        using UserTable = std::unordered_map<std::string, std::string>;

        explicit Controller(
            coordination::Assembler&  assembler,
            coordination::UsersGroup& users_group,
            UserTable                 users = {}
        ) noexcept : assembler_(assembler), users_group_(users_group), users_(std::move(users)) {}

        // 引用成员会隐式删除拷贝赋值，并把不可赋值性传染给任何持有者。
        // 显式 delete 使「不可拷贝」成为写明的意图而非副作用
        // 注意：显式 delete 拷贝构造会连带抑制隐式移动构造
        // Controller 因此既不可拷贝也不可移动，不能放进 vector（同 core::Connections）
        Controller(const Controller&)            = delete;
        Controller& operator=(const Controller&) = delete;

        bool auth(const std::string& uid, const std::string& credential) const;
        bool register_user(const std::string& uid, int fd);
        bool delete_fd(int fd);

        void process(int fd, const MessagePack& rmp, std::vector<std::pair<MessagePack, int>>& out_queue);
        bool query(const std::string& uid, int& fd) const;

    private:
        coordination::Assembler&  assembler_;
        coordination::UsersGroup& users_group_;
        UserTable                 users_;
    };
} // controller


#endif //COM_LITE_CONTROLLER_H
