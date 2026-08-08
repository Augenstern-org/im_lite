//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_USERSGROUP_H
#define COM_LITE_USERSGROUP_H

// #include <cstdint>
#include <cassert>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "server/coordination/user.h"

namespace coordination {
    // register_user 的三态结果。[[nodiscard]] 标注在类型上，
    // 于是任何返回该类型的函数（含 Controller::register_user）丢弃返回值都会告警。
    enum class [[nodiscard]] RegisterResult {
        Registered,   // 绑定成功（新用户，或同一 uid 的新 fd）
        AlreadyBound, // 该 fd 已绑定到同一 uid —— 幂等成功，两表未改动
        FdConflict    // 该 fd 已绑定到另一个 uid —— 失败，两表未改动
    };

    // 目前为单线程设计，详见 TODO.md
    class UsersGroup {
        // 之后采用数据库储存
        // uid <-> user(fds)
        std::unordered_map<std::string, User> uid_to_fd_;
        std::unordered_map<int, std::string> fd_to_uid_;

    public:
        // 类不变式：uid_to_fd_ 与 fd_to_uid_ 互为精确的逆映射。
        // 本身不分配内存、不抛异常。除内部 assert 外，测试需显式调用它逐步校验 ——
        // 内部 assert 在 NDEBUG 下会整个消失，不能当作验证手段依赖。
        //
        // 两个方向都必须扫，缺一不可：单向扫描只能走到「被扫的那张表指得到」的条目，
        // 于是两类错误可以互相抵消。具体地，若只有正向扫 + 末尾总数相等这一道，
        //   uid_to_fd_ == {alice: [5, 5]}、fd_to_uid_ == {5 -> alice, 7 -> bob}
        // 会被判成合法：正向每一步都对得上（5 两次都反查回 alice），
        // total_fds == 2 又恰好等于 fd_to_uid_.size() == 2 —— 重复登记的 5 多算的那一份，
        // 正好把孤儿条目 7 的差额补平。两种错误单独出现时都抓得住
        //（只有 [5, 5] 时 2 != 1；只有孤儿 7 时 total_fds < size），漏的只是这种成对抵消。
        // 反向扫要求 fd_to_uid_ 的每一条都能在对应用户的 fds_ 里恰好命中一次，
        // [5, 5] 与孤儿 7 各自独立被否掉，抵消不再可能。
        bool invariants_ok() const noexcept {
            // ── 正向：uid_to_fd_ -> fd_to_uid_ ──
            for (const auto& entry : uid_to_fd_) {
                const std::vector<int>& fds = entry.second.vec();
                // 不允许存在一个 fd 都没有的用户条目。这条两个方向的扫描都覆盖不到
                //（空向量既不产生正向待查项，也不会被任何反向条目引用到），必须单独挡
                if (fds.empty()) return false;

                for (const int fd : fds) {
                    const auto fd_it = fd_to_uid_.find(fd);
                    // 该 fd 必须在 fd 表中，且必须反查回同一个 uid
                    if (fd_it == fd_to_uid_.end()) return false;
                    if (fd_it->second != entry.first) return false;
                }
            }

            // ── 反向：fd_to_uid_ -> uid_to_fd_ ──
            // 嵌套线性扫描是有意的：要保持 noexcept 且零分配，就不能引入 set / 计数表。
            // 单个用户的 fds_ 只有个位数条目，而本函数只在 assert 与测试里跑。
            for (const auto& entry : fd_to_uid_) {
                const auto uid_it = uid_to_fd_.find(entry.second);
                // 该 uid 必须存在，否则是一条谁也认领不了的孤儿 fd
                if (uid_it == uid_to_fd_.end()) return false;

                // 且该用户的 fds_ 里必须「恰好」出现这个 fd 一次：
                // 0 次同样是孤儿（uid 在、但没人指回来）；2 次及以上是重复登记 ——
                // delete_fd 摘掉一份后向量仍非空，会残留一条指向已关闭 fd 的
                // uid -> {5}（见 register_user 的 AlreadyBound 分支注释）
                std::size_t hits = 0;
                for (const int fd : uid_it->second.vec()) {
                    if (fd == entry.first) ++hits;
                }
                if (hits != 1) return false;
            }

            // 不再校验 total_fds == fd_to_uid_.size()：两个方向都精确之后它严格冗余。
            // 正向保证「向量条目 -> fd 键」是单射（同一 fd 落进两个不同用户的向量，
            // 会在正向反查 uid 时自相矛盾；落进同一向量两次，会被反向的 hits != 1 否掉），
            // 反向保证它是满射（每个 fd 键在对应用户的向量里恰好有一个原像）。
            // 既然是双射，两表元素总数必然相等，再数一次数不出任何新东西。
            return true;
        }

        // 三态注册：先分类、后提交。
        // 分类阶段只查表、不改动任何一张表，因此两处变更只在「两次插入都必然成功」
        // 时才可达 —— 既没有半提交窗口，也就没有需要写对的回滚代码。
        // 不再是 noexcept：提交阶段会分配内存。
        RegisterResult register_user(const std::string& uid, int fd) {
            // ── 分类：一次 find 定性，两表保持原样 ──
            const auto fd_it = fd_to_uid_.find(fd);
            if (fd_it != fd_to_uid_.end()) {
                // 同一 fd 重复登录同一 uid：幂等成功，不得把 fd 再追加进 fds_。
                // 否则 fds_ == [5, 5]，delete_fd 摘掉一份后向量仍非空，
                // 会残留一条指向已关闭 fd 的 uid -> {5}
                if (fd_it->second == uid) return RegisterResult::AlreadyBound;

                // 同一 fd 改登另一个 uid：拒绝。若放行，fd 表的插入会因键已存在而
                // 静默失败、uid 表却写入成功，两表就此互相矛盾；断连时清理不到的
                // 那条残留会在 OS 回收 fd 后把消息投递给无关连接
                //（docs/architecture.md §3.4）
                return RegisterResult::FdConflict;
            }

            // ── 提交：走到这里两次插入都不会失败 ──
            // 注意：两次变更之间若抛 bad_alloc，两表会不一致。这里有意接受该后果为
            // 致命错误，不写回滚 —— 两表随连接数无界增长，预留容量没有意义，且进程
            // 本身没有 OOM 恢复路径（src/server/main.cpp:47-51 捕获后直接退出事件循环）
            [[maybe_unused]] const bool fd_inserted = fd_to_uid_.emplace(fd, uid).second;
            assert(fd_inserted);

            // 添加 uid 表的第一个 fd；uid 已存在则是同一用户的又一台设备
            const std::pair<std::unordered_map<std::string, User>::iterator, bool> uid_ins =
                uid_to_fd_.try_emplace(uid, fd);
            if (!uid_ins.second) uid_ins.first->second.add(fd);

            assert(invariants_ok());
            return RegisterResult::Registered;
        }

        bool delete_fd(int fd) {
            // fd -> uid
            const auto fd_it = fd_to_uid_.find(fd);
            if (fd_it == fd_to_uid_.end()) return false;
            // 必须按值取：下面会销毁 fd_to_uid_ 的节点，取引用会悬垂
            const std::string uid = fd_it->second;

            // uid -> User
            const auto it = uid_to_fd_.find(uid);
            if (it == uid_to_fd_.end()) return false;

            // 在这行之后需保证不会出现异常
            fd_to_uid_.erase(fd_it);

            // 管理 uid 表：先摘掉该设备的 fd
            User& user = it->second;
            user.delete_device(fd);

            // 该用户已无任何 fd，整条记录一并删除
            // 这必须是 user 与 it 的最后一次使用——erase 之后两者都已失效
            if (user.vec().empty()) uid_to_fd_.erase(it);

            assert(invariants_ok());
            return true;
        }


        bool has_fd(int fd) const {
            // 连接级登录态判定：fd 在双向映射中即已登录（architecture.md §3.4 步 2）。
            return fd_to_uid_.find(fd) != fd_to_uid_.end();
        }

        bool query(const std::string& uid, int& fd) const {
            // 无用户
            auto find = uid_to_fd_.find(uid);
            if (uid_to_fd_.end() == find) return false;

            // 无fd
            // 其实这里可能过度防御了，不过性能开销不大，就加上了
            int _fd = -1;
            if (!find->second.get_fd(_fd)) return false;

            // 正常返回
            fd = _fd;
            return true;
        }

        // 必须传入指针，成功返回只读向量地址，失败返回空指针
        bool query_all(const std::string& uid, const std::vector<int>*& fds) const {
            auto find = uid_to_fd_.find(uid);
            if (uid_to_fd_.end() == find) {
                fds = nullptr;
                return false;
            }

            fds = &(find->second.vec());
            return true;
        }
    };
} // coordination

#endif //COM_LITE_USERSGROUP_H
