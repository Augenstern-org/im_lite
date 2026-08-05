#include "server/coordination/users_group.h"

#include "test_support.h"

#include <iostream>
#include <string>

int main() {
    using coordination::UsersGroup;

    // ── 注册新用户 → 查询返回正确 fd ──────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("alice", 42));

        int fd = -1;
        CHECK(g.query("alice", fd));
        CHECK(fd == 42);
    }

    // ── 查询不存在的 uid → 返回 false，fd 不变 ────────────
    {
        UsersGroup g;
        int fd = -1;
        CHECK(!g.query("nobody", fd));
        CHECK(fd == -1);
    }

    // ── 重复注册同一 uid 不同 fd → 保留第一次的 fd（幂等） ─
    {
        UsersGroup g;
        CHECK(g.register_user("bob", 10));
        CHECK(g.register_user("bob", 99));

        int fd = -1;
        CHECK(g.query("bob", fd));
        CHECK(fd == 10);
    }

    // ── 按 fd 删除存在的用户 → 返回 true，之后查不到 ──────
    {
        UsersGroup g;
        CHECK(g.register_user("carol", 7));

        CHECK(g.delete_fd(7));

        int fd = -1;
        CHECK(!g.query("carol", fd));
    }

    // ── 删除不存在的 fd → 返回 false ─────────────────────
    {
        UsersGroup g;
        CHECK(!g.delete_fd(999));
    }

    // ── 删除后可重新注册同一 uid ──────────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("dave", 3));
        CHECK(g.delete_fd(3));
        CHECK(g.register_user("dave", 88));

        int fd = -1;
        CHECK(g.query("dave", fd));
        CHECK(fd == 88);
    }

    // ── 多个用户并存，互不干扰 ─────────────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("u1", 1));
        CHECK(g.register_user("u2", 2));
        CHECK(g.register_user("u3", 3));

        int fd = -1;
        CHECK(g.query("u1", fd) && fd == 1);
        CHECK(g.query("u2", fd) && fd == 2);
        CHECK(g.query("u3", fd) && fd == 3);

        // 删除 u2 的 fd(2) 不影响 u1 和 u3
        CHECK(g.delete_fd(2));
        CHECK(g.query("u1", fd) && fd == 1);
        CHECK(!g.query("u2", fd));
        CHECK(g.query("u3", fd) && fd == 3);
    }

    // ── 删除后 fd 释放，可被新用户使用 ────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("eve", 8));
        CHECK(g.delete_fd(8));
        CHECK(g.register_user("frank", 8));

        int fd = -1;
        CHECK(!g.query("eve", fd));
        CHECK(g.query("frank", fd) && fd == 8);
    }

    // ── 批量注册 / 删除 ──────────────────────────────────
    {
        UsersGroup g;
        for (int i = 1; i <= 10; ++i) {
            std::string uid = "user" + std::to_string(i);
            CHECK(g.register_user(uid, i));
        }

        int fd = -1;
        CHECK(g.query("user5", fd) && fd == 5);

        for (int i = 1; i <= 5; ++i) {
            CHECK(g.delete_fd(i));
        }
        CHECK(!g.query("user1", fd));
        CHECK(!g.query("user5", fd));
        CHECK(g.query("user6", fd) && fd == 6);
    }

    // ── 空表查询与删除 ───────────────────────────────────
    {
        UsersGroup g;
        int fd = -1;
        CHECK(!g.query("anyone", fd));
        CHECK(!g.delete_fd(1));
    }

    // ── has_fd：连接级登录态判定 ─────────────────────────
    {
        UsersGroup ug;

        // 未注册的 fd → false
        CHECK(!ug.has_fd(11));

        // 注册后 → true
        CHECK(ug.register_user("carol", 11));
        CHECK(ug.has_fd(11));

        // 删除后 → false
        CHECK(ug.delete_fd(11));
        CHECK(!ug.has_fd(11));
    }

    std::cout << "test_users_group: PASSED\n";
    return 0;
}
