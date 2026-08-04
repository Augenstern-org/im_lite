#include "server/coordination/users_group.h"

#include "test_support.h"

#include <iostream>
#include <string>

int main() {
    using coordination::UsersGroup;

    // ── 注册新用户 → 查询返回正确 fd ──────────────────────
    {
        UsersGroup g;
        g.register_user("alice", 42);

        int fd = -1;
        CHECK(g.query("alice", fd));
        CHECK(fd == 42);
    }

    // ── 查询不存在的 uid → 返回 false ─────────────────────
    {
        UsersGroup g;
        int fd = -1;
        CHECK(!g.query("nobody", fd));
        // fd 在失败路径上不应被修改
        CHECK(fd == -1);
    }

    // ── 重复注册同一 uid → 第一次的 fd 被保留（幂等） ────
    {
        UsersGroup g;
        g.register_user("bob", 10);
        g.register_user("bob", 99);  // 第二次注册应被忽略

        int fd = -1;
        CHECK(g.query("bob", fd));
        CHECK(fd == 10);  // 仍是第一次的 fd
    }

    // ── 删除存在的用户 → 返回 true，之后查不到 ────────────
    {
        UsersGroup g;
        g.register_user("carol", 7);

        CHECK(g.delete_fd("carol"));

        int fd = -1;
        CHECK(!g.query("carol", fd));
    }

    // ── 删除不存在的用户 → 返回 false ─────────────────────
    {
        UsersGroup g;
        CHECK(!g.delete_fd("ghost"));
    }

    // ── 删除后可重新注册同一 uid ──────────────────────────
    {
        UsersGroup g;
        g.register_user("dave", 3);
        g.delete_fd("dave");
        g.register_user("dave", 88);

        int fd = -1;
        CHECK(g.query("dave", fd));
        CHECK(fd == 88);
    }

    // ── 多个用户并存，互不干扰 ─────────────────────────────
    {
        UsersGroup g;
        g.register_user("u1", 1);
        g.register_user("u2", 2);
        g.register_user("u3", 3);

        int fd = -1;
        CHECK(g.query("u1", fd) && fd == 1);
        CHECK(g.query("u2", fd) && fd == 2);
        CHECK(g.query("u3", fd) && fd == 3);

        // 删除 u2 不影响 u1 和 u3
        CHECK(g.delete_fd("u2"));
        CHECK(g.query("u1", fd) && fd == 1);
        CHECK(!g.query("u2", fd));
        CHECK(g.query("u3", fd) && fd == 3);
    }

    std::cout << "test_users_group: PASSED\n";
    return 0;
}
