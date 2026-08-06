#include "server/coordination/users_group.h"

#include "test_support.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

int main() {
    using coordination::RegisterResult;
    using coordination::UsersGroup;

    // ── 注册新用户 → 查询返回正确 fd ──────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("alice", 42) == RegisterResult::Registered);

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
        CHECK(g.register_user("bob", 10) == RegisterResult::Registered);
        CHECK(g.register_user("bob", 99) == RegisterResult::Registered);

        int fd = -1;
        CHECK(g.query("bob", fd));
        CHECK(fd == 10);
    }

    // ── 按 fd 删除存在的用户 → 返回 true，之后查不到 ──────
    {
        UsersGroup g;
        CHECK(g.register_user("carol", 7) == RegisterResult::Registered);

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
        CHECK(g.register_user("dave", 3) == RegisterResult::Registered);
        CHECK(g.delete_fd(3));
        CHECK(g.register_user("dave", 88) == RegisterResult::Registered);

        int fd = -1;
        CHECK(g.query("dave", fd));
        CHECK(fd == 88);
    }

    // ── 多个用户并存，互不干扰 ─────────────────────────────
    {
        UsersGroup g;
        CHECK(g.register_user("u1", 1) == RegisterResult::Registered);
        CHECK(g.register_user("u2", 2) == RegisterResult::Registered);
        CHECK(g.register_user("u3", 3) == RegisterResult::Registered);

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
        CHECK(g.register_user("eve", 8) == RegisterResult::Registered);
        CHECK(g.delete_fd(8));
        CHECK(g.register_user("frank", 8) == RegisterResult::Registered);

        int fd = -1;
        CHECK(!g.query("eve", fd));
        CHECK(g.query("frank", fd) && fd == 8);
    }

    // ── 批量注册 / 删除 ──────────────────────────────────
    {
        UsersGroup g;
        for (int i = 1; i <= 10; ++i) {
            std::string uid = "user" + std::to_string(i);
            CHECK(g.register_user(uid, i) == RegisterResult::Registered);
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
        CHECK(ug.register_user("carol", 11) == RegisterResult::Registered);
        CHECK(ug.has_fd(11));

        // 删除后 → false
        CHECK(ug.delete_fd(11));
        CHECK(!ug.has_fd(11));
    }

    // ── 同 fd 同 uid 重复注册 → AlreadyBound，两表未改动 ──
    {
        UsersGroup g;
        CHECK(g.register_user("alice", 5) == RegisterResult::Registered);
        CHECK(g.register_user("alice", 5) == RegisterResult::AlreadyBound);
        CHECK(g.invariants_ok());

        // 回归守卫：若第二次注册把 fd 追加成 fds_ == [5, 5]，
        // 一次 delete_fd 摘掉一份后向量仍非空，alice 就会残留在线
        CHECK(g.delete_fd(5));

        int fd = -1;
        CHECK(!g.query("alice", fd));
        CHECK(!g.has_fd(5));
        CHECK(g.invariants_ok());
    }

    // ── 同 fd 改登另一个 uid → FdConflict，两表未改动 ─────
    {
        UsersGroup g;
        CHECK(g.register_user("alice", 5) == RegisterResult::Registered);
        CHECK(g.register_user("bob", 5) == RegisterResult::FdConflict);
        CHECK(g.invariants_ok());

        int fd = -1;
        CHECK(!g.query("bob", fd));
        CHECK(g.query("alice", fd) && fd == 5);

        // 回归守卫：alice 断连后不得残留 bob -> {5}。
        // 该残留会在 OS 回收 fd 5 之后把 bob 的消息投递给一条无关连接
        CHECK(g.delete_fd(5));
        CHECK(!g.query("bob", fd));
        CHECK(!g.query("alice", fd));
        CHECK(!g.has_fd(5));
        CHECK(g.invariants_ok());
    }

    // ── 单 uid 多设备：最后一个 fd 断开才真正下线 ──────────
    {
        UsersGroup g;
        CHECK(g.register_user("alice", 5) == RegisterResult::Registered);
        CHECK(g.register_user("alice", 6) == RegisterResult::Registered);
        CHECK(g.invariants_ok());

        int fd = -1;
        CHECK(g.delete_fd(5));
        CHECK(!g.has_fd(5));
        CHECK(g.query("alice", fd) && fd == 6); // 仍在线
        CHECK(g.invariants_ok());

        CHECK(g.delete_fd(6));
        CHECK(!g.query("alice", fd));
        CHECK(!g.has_fd(6));
        CHECK(g.invariants_ok());
    }

    // ── 属性测试：随机操作序列 vs 朴素模型 ─────────────────
    {
        // 字母表刻意取小（5 uid × 5 fd），逼出重复注册、改绑与 fd 复用
        const std::string uids[5] = {"u0", "u1", "u2", "u3", "u4"};
        const int         fds[5]  = {3, 4, 5, 6, 7};

        UsersGroup g;
        // 朴素模型：按注册先后排列的 (fd, uid) 绑定表。
        // 顺序有意义 —— User::fds_ 也是按注册顺序追加、按值删除，
        // 因此某 uid 在模型里最早的那条就是 query 应当返回的 fd
        std::vector<std::pair<int, std::string>> model;

        std::mt19937                       rng(20260806u); // 固定种子：失败必须可复现
        std::uniform_int_distribution<int> pick(0, 4);
        std::uniform_int_distribution<int> op(0, 2); // 0/1 → 注册，2 → 删除

        for (int step = 0; step < 5000; ++step) {
            if (op(rng) != 2) {
                const std::string& uid = uids[pick(rng)];
                const int          reg_fd = fds[pick(rng)];

                // 由模型预判三态结果
                const auto it = std::find_if(
                    model.begin(), model.end(),
                    [reg_fd](const std::pair<int, std::string>& e) { return e.first == reg_fd; });

                RegisterResult expected = RegisterResult::Registered;
                if (it != model.end()) {
                    expected = (it->second == uid) ? RegisterResult::AlreadyBound : RegisterResult::FdConflict;
                }

                CHECK(g.register_user(uid, reg_fd) == expected);
                if (expected == RegisterResult::Registered) model.emplace_back(reg_fd, uid);
            } else {
                const int del_fd = fds[pick(rng)];

                const auto it = std::find_if(
                    model.begin(), model.end(),
                    [del_fd](const std::pair<int, std::string>& e) { return e.first == del_fd; });
                const bool expected = it != model.end();

                CHECK(g.delete_fd(del_fd) == expected);
                if (expected) model.erase(it);
            }

            // 不变式必须显式查：内部 assert 在 NDEBUG 下会整个消失
            CHECK(g.invariants_ok());

            // 与模型对账 —— fd 侧
            for (const int fd : fds) {
                const bool in_model = std::find_if(model.begin(), model.end(),
                                                   [fd](const std::pair<int, std::string>& e) {
                                                       return e.first == fd;
                                                   }) != model.end();
                CHECK(g.has_fd(fd) == in_model);
            }

            // 与模型对账 —— uid 侧
            for (const std::string& uid : uids) {
                const auto it = std::find_if(model.begin(), model.end(),
                                             [&uid](const std::pair<int, std::string>& e) {
                                                 return e.second == uid;
                                             });

                // 先落定调用，再断言：不要把 query 与它的出参塞进同一个表达式，
                // 求值顺序未指定时出参会读到旧值
                int        fd  = -1;
                const bool got = g.query(uid, fd);

                CHECK(got == (it != model.end()));
                if (got) CHECK(fd == it->first);
            }
        }
    }

    std::cout << "test_users_group: PASSED\n";
    return 0;
}
