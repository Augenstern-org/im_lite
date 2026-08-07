# com_lite TODO

> 本文件是本仓库**唯一**的开放事项清单。新的待办一律记在这里，不要再往
> `docs/architecture.md §7` 或 `docs/logs/*.md` 的「已知未收敛点」里各记一份 ——
> 那两处是历史快照，只读。锚点格式 `file:line`，以代码为准。

### 待办

- [ ] **跨 fd 转发 — fan-out**（`pending_close_` 排除与 `EPOLLOUT` 武装已消费，剩最后一子项）
  `src/server/core/core.cpp:219`
  > 基础通路已落地（`controller.cpp:78` 的 `out_queue.emplace_back(rmp, to_fd)`，
  > `core.cpp:226` 的 `conns_.find(to_fd)` 查目标连接写入）。
  > **2026-08-06** 消费 `pending_close_` 排除（连带把失败归因从「杀发送方」改成「杀目标」）；
  > **2026-08-07** 消费 `EPOLLOUT` 武装。标题已改：只剩 fan-out，**不要把它读成「转发已经做完了」**。
  - [x] 目标 fd 的 `EPOLLOUT` 无人武装：`update_events()`（`core.h:45`，私有）只对**当前** fd
    调用一次。向另一个 fd 部分写入后，没有任何地方会给它挂上 `EPOLLOUT`，该连接的出站数据会
    **静默无限期滞留**。反方向同样要小心：LT 模式下 `EPOLLOUT` 挂在空闲可写的 socket 上会让
    每次 `epoll_wait` 立刻返回，形成 CPU 空转。规则是「写缓冲非空才武装，排空即撤」。
    > 2026-08-07 落地：`send()` 返回 `would_block`（背压，非失败——数据已全部进入目标
    > `write_buf_`，`Connections::send` 先 insert 后 flush）时，跨 fd 给目标挂
    > `update_events(to_fd, to_conn)`（`core.cpp:253-258`），排空后掩码重算自动摘除
    > （`core.cpp:294-305`），LT 空转不成立（`EPOLLOUT` 只在 socket 可写时报告，EAGAIN 即不可写）；
    > 自发自收无需处置（`handle_client` 末尾的 `update_events(fd, conn)` 已按 `want_write()` 挂上）。
    > 验证（M8/M9/M10 双进程）：慢消费突发 200×4KB 全送达（改前滞留收不全）；停读 12MB 顶爆
    > 高水位，归因仍杀目标（`[core] send to fd 5 failed (Error); closing target, sender fd 6
    > unaffected`），发送方不受牵连。
  - [x] 目标 fd 可能已在 `pending_close_` 里：`close_client()`（`core.cpp:308-323`）只做
    `EPOLL_CTL_DEL` + 入队，真正的 `erase` 推迟到 `drain_pending_close()`（`core.cpp:335`）。
    因此 `conns_.find(target_fd)` **仍能命中**一个已被摘出 epoll、即将销毁的连接。
    跨 fd 写入必须同时排除 `pending_close_` 中的 fd。
    > 2026-08-06 落地：新增 `Core::is_pending_close()`（`core.cpp:325`，`run()` 原地内联的扫描
    > 也改成调用它）；出队循环 `core.cpp:227` 用它排除已判死的目标。同一轮顺带修掉 D3：三种失败
    > 模式各自归因 —— 目标不可投递 / 编码失败只丢该条并 `continue`（不再 `break`，发送方排在转发
    > 之后的回执不会被吞），写目标失败改为 `close_client(to_fd)` 杀**目标**而非发送方；自发自收
    > （`to_fd == fd`）回落到唯一的入站杀点。`close_client()` 同时改为幂等，避免同一 fd 重复入队
    > 触发两次 `disconnect_handler_`。不变式 (c) 已按「仅对入站 fd 为 0」重写（`core.cpp:167`）。
  - [ ] fan-out：`to_uid_` → fd 集合的解析与多 fd 分发（协调层职责，
    见 `docs/architecture.md` §2.2）。前置已满足：`user.h:14` 的 `fds_` 向量 + `vec()`
    （`user.h:44-50`）已落地（2026-08-06，commit 2e315b2）；`UsersGroup::query` 仍取 `fds_[0]`
    （`user.h:36-42`），一次请求只投递目标 uid 的第一个在线设备。

- [x] **`UsersGroup` 没有接入点** `src/server/coordination/users_group.h`
  > 2026-08-04 落地：键统一为 `std::string`，新增 `register_user` / `delete_fd` / `query`，双向映射 `uid_to_fd_` + `fd_to_uid_`；
  > `disconnect_handler` 已调用 `controller.delete_fd(fd)` 解绑；纯头文件无需独立 CMake target。
  - [x] 键类型不匹配：`group_` 的键是 `uint32_t`，而 `Message::from_uid_` / `to_uid_`
    （`src/common/message.h:34-35`）是 `std::string`。二者必须先统一。
  - [x] 断开时解绑没有落点：`main.cpp` 的 disconnect 回调只打印日志。`docs/architecture.md:169-178`
    要求断开即从用户组移除 fd，否则 fd 号被新连接复用后会**把消息发给错的人**。
  - [x] `users_group.cpp` 不属于任何 CMake target（`src/server/CMakeLists.txt`）。一旦给
    `UsersGroup` 补外联成员函数，`server` 立即 undefined reference。是建 `coordination`
    独立库还是并入 `server` 可执行，随接入一并定。

- [ ] **异常边界下沉** `src/server/main.cpp`
  `try/catch` 包住的是整个 `run()`，任何一条消息触发的异常都会终结事件循环、连带断开所有连接。
  单连接的错误不应有这样的影响半径，catch 应下沉到 `handle_client()` 内部。

- [ ] **`response` opcode 被静默丢弃** `src/server/controller/controller.cpp`
  `process()` 只分派 `ack` 与 `request`，其余落到 else 直接 return —— 既不入队也不报错。
  服务端本就不该收到 `response`，但「不该收到」和「收到了当没看见」是两件事。

- [ ] **应答 / 响应帧的帧体形态未定**
  - [ ] `ack` 的帧体是占位：`content_ = '\x01'`，长度 1、内容为字节 0x01 的字符串，
    不是任何约定的应答体（`src/server/coordination/assembler.h:44-52`）。
  - [ ] `request` 的响应帧当前把入站 `Message` 原样复制，只把 opcode 翻成 `response`。
    真正该带的是服务端消息序号 —— 即下一条。

- [ ] **`ResponseMsg::server_seq_` 的去向** `docs/architecture.md:229`
  随消息模型塌缩去掉，尚未确定以何种形式回归：`Message` 的可选字段，还是移进帧头。

- [x] **登录 / 握手协议** `docs/architecture.md:227`
  > 2026-08-05 落地：新增 `Opcode::login`（线上字节 3，`src/common/binary_code.h`），登录帧复用
  > `Message` 结构（`from_uid_` = 待登录 uid，`content_` = 凭证，`to_uid_` 填自身过 `is_valid` 闸）。
  > `Controller` 构造注入静态用户表（uid → 凭证，`main.cpp` 演示表），`auth(uid, credential)` 查表比对；
  > 成功 `register_user(uid, fd)` 绑定并回 `response(ok)`，失败回 `response(fail)` 且连接保留可重试。
  > accept 阶段不再注册任何 uid（`core.cpp` 原 `register_handler_("guest", …)` 删除，§3.4 步 2 兑现）；
  > 未登录连接发非登录帧一律拒绝并记日志（`controller.cpp` login 分支）。`core` 的
  > `auth_handler_` / `register_handler_` 回调随登录驱动归协调层而退役（8/4 日志 §4.5 同步消除）。

- [ ] **分帧状态机** `src/common/decoder.h:25`
  范围说明（2026-08-03 改写）：本条现在**只为流式增量解析（性能议题）存在**——不把整帧攒在
  `read_buf_` 里就能推进解析。与错误路径正确性无关：docs/logs/2026-07-31.md §4.1 已推翻
  「必须等分帧状态机给出『本次消耗了多少字节』的出参」的前提——ok 路径消耗量 =
  `FrameHeader::wire_size + out_rmp.fh_.body_len_` 今天可得，`core.cpp:200` 的 drain 循环已按
  精确 erase 推进。字节级切分、「暂时不完整 vs 协议错误」的区分（`incomplete`，decoder.h:32
  帧头未齐 / :55 帧体未齐，保留字节）、超长帧关连接处置（`frame_too_long` → fatal → 关连接，
  core.cpp:188-196/:283）均已随「核心层错误路径三连」落地，不再是本条的待办。

- [ ] **帧完整性校验**：magic + CRC16，尚未设计。

- [ ] **零拷贝编码路径** `src/common/encoder.h:44`、`:105`、`src/server/core/core.cpp:236`
  依赖项（原「编码缓冲按条分配」）已落地：`encode_buf_` 跨消息、跨连接复用并按需增长，
  每条消息 65536 字节的分配与清零已经消失。这条还剩的是**拷贝次数** —— 一条出站帧仍要经过
  三次搬运：`body_serializer()` 先 `dump()` 出一个 `std::string`，`body_writer()` 再 `memcpy`
  进 `out_buf`，`conn.send()` 又把 `[0, out_len)` 拷进 `write_buf_`。
  - [ ] 消除前两次要让序列化直接落进 `out_buf`。难点是顺序：帧体长度必须在写入之前已知，
    超长帧才拦得住（`encoder.h:81` 零分配预检 / `:87` 防膨胀后置防线）；流式写入会让
    「先算长度再写」的前提失效，`encoder.h:67` 那条「出错时 `out_buf` 一字节未动」的
    后置条件也要重新设计。
  - [ ] 消除第三次要让编码直接写进连接的写缓冲，那会把编码缓冲的生命周期与 `Connections`
    绑死，与「复用缓冲归调用方持有」的现有边界相冲，得连带重新划线。

- [ ] **调试观测点缺失** `src/server/controller/controller.cpp`
  > 2026-08-05 部分补齐：登录协议落地时补了「未登录连接被拒」日志（opcode + fd，login 分支），
  > 普通消息路径（转发 / 回显 / 丢弃）仍无输出，本条保留。
  原先打印 `from_uid_` / `content_` 的两行已删，服务端对收到的普通消息没有任何输出，
  端到端联调只能靠客户端侧的回显判断。

- [ ] **`docs/architecture.md` 与代码漂移**
  - [x] `:202` 的 handler 签名仍写 `std::vector<MessagePack>&`，实际是
    `std::vector<std::pair<MessagePack, int>>&`。→ 2026-08-04 已修正。
  - [x] `:205`「仍为回显」与 `:30` 的骨架清单已过时。→ 2026-08-04 已更新。
  - [ ] `:167` 引用的 `main.cpp` 行号（`:14` / `:19` / `:29`）与实际不符。
  - [ ] `:228`「超长帧防护」给次序硬约束列了**两条**理由，其中一条已作废：原文写
    「`Encoder::encode()` 在序列化完成之后、**向 `out_buf` 写入之前**拒绝——这个次序是硬约束，
    写在 `memcpy` 之后等于越界已经发生，且会破坏 `encode()` 自述的『返回错误时 `out_buf`
    一字节未动』后置条件」。前一条（越界）已不再成立：`encode()` 现在自己按需增长 `out_buf`
    （`src/common/encoder.h:101`），调用方不再负责容量，写多少都不会越界。后一条
    （`out_buf` 一字节未动）仍然成立。次序硬约束本身**依然是硬约束**，只是现役理由变成
    「防 DoS 放大：先分配后校验等于让 6 字节头诱导一次 64KB 分配」与「`resize` 会抛
    `bad_alloc`，必须排在 `body_len_` 回写之前，失败路径才不留下半写状态」——
    两条都记在 `encoder.h:87-94` 的注释里。→ architecture.md §7 超长帧防护段已缩短，
    此条目所指的具体文字已变更，相关理由以 `encoder.h` 注释为准。
  - [x] `§7 待定项`（`:223-232`）改为指向本文件，不再各自维护一份。→ 2026-08-04 已添加指向。

- [ ] `UsersGroup` `注册fd` `删除fd` 改为多线程设计。
- [x] `register_handler` 暂时不能正确地将 `fd` 添加到 `uid_to_fd_` / `fd_to_uid_` 两哈希表，因为 `accept_client` 阶段不能读取 `uid`。
  > 2026-08-05 随登录协议落地而消除：accept 阶段不再注册，绑定改在登录帧处理时
  > （`controller.cpp` login 分支 `register_user(m.from_uid_, fd)`）。

- [ ] **登录防爆破**：登录失败当前无次数限制（2026-08-05 决策「fail 可重试」，连接可无限重试）。
  后续做次数限制 / 拉黑（连续 N 次失败关连接或封禁）。`controller.cpp` login 分支。

### 有意缺失 / 已划定的边界

以下**不是**待办，是已经做出的决定。要改动它们，得先推翻决定本身。

- **装配器不做路由。** `coordination::Assembler` 的签名里没有 fd，也不持有 `UsersGroup` ——
  它只负责把出站 `MessagePack` **装配**出来，「送到哪个 fd」由调用方决定。在跨 fd 转发
  （本文件第 1 条）落地之前，出站帧的目标 fd 恒等于入站 fd；因为装配器无法表达 fd，
  误投在类型上不可发生。
- **`Encoder` 不做有效性校验**（`src/common/encoder.h:75-76`）。
  出站数据由程序自己构造，视为可信；入站数据才是敌意的。编解码不对称是有意的（2026-07-27 决策）。
  原文附带的「不检查 `out_buf` 容量」已随下一条作废：容量不再是调用方的义务，`encode()` 自己
  按需增长 `out_buf`。不做的仍然是 `rmp.is_valid()` 这类**内容**校验。
- **编码复用缓冲归调用方持有，`Encoder` 保持无状态**（2026-07-31 决策，取代 2026-07-29 的
  「编码器内部缓冲明确推迟」）。触发条件已到并已落地 —— 但**解决形式与当初设想不同**：
  复用缓冲是 `core::Core::encode_buf_`（`src/server/core/core.h:58`），`encode()` 依旧是 static、
  只把出参按需增长（`src/common/encoder.h:101`，只增不减），编码器本身不持有任何状态。
  不采用「编码器自持缓冲」的理由：那样 `Encoder` 就得对外返回一个指向内部缓冲的视图
  （指针 / span），而该视图会被**下一次 `encode()` 静默失效** —— 调用方手里是一块随时可能被
  覆写、甚至因 realloc 而悬空的内存，失效时机取决于编码器内部状态，类型上无从表达、
  编译器也查不出来。缓冲归调用方持有时生命周期归属明确：谁持有谁负责，`out_buf` 活多久
  由调用方自己的作用域决定。
- **`Decoder` 保持无状态，分帧缓冲归连接侧。** 与上一条同源的边界，方向相反。
  (1) 分帧缓冲是**每条 TCP 流**的跨调用状态，而 `Decoder::decode`（`src/common/decoder.h:26`）
  是 static、全服务端只有一个逻辑解码器；给它加成员缓冲会让连接 A 的残帧与连接 B 后续到达的
  字节拼接在一起，直接跨连接串包。
  (2) `Connections::read_buf_` 已经在做累积且不预分配，再加一层 `Decoder::buf_` 只是多一跳全量拷贝。
  附带记一条今天成立、将来要盯住的性质：解码器**确实按 `body_len` 分配**包体字符串
  （`decoder.h:57-60`，长度恰为 `body_len`），但那次分配排在**两道**校验之后 ——
  `decoder.h:48` 的上限校验，与 `decoder.h:55` 的 `in_buf.size() - wire_size < body_len`
  （即声称的字节确已全部到达）。因此当前的放大比是 **1:1**：要让服务端分配 N 字节，
  对端必须真的把 N 字节发过来。这不是「零分配」，是「分配量被实际到达量卡住」。
  将来若为性能加 `reserve(body_len)`、或按 `body_len` 预留读缓冲，危险**不是**新增放大面，
  而是**绕过 `:55` 这道已经在挡的闸** —— `reserve` 只需 6 字节头就能触发，放大比会从 1:1
  直接跳到 6:65536。届时 `decoder.h:48` 的上限校验**必须**排在任何按 `body_len` 定尺寸的
  动作之前，且预留量不得超过已到达字节数。编码侧 `encoder.h:83`（校验）与 `:101`（resize）
  的先后顺序就是这条规则的现成范例。
- **离线消息暂存暂不实现**（`docs/architecture.md:230`）。目标 uid 无在线 fd 时的处理不落地，
  仅在协调层预留位置。分层边界不受影响。
- **性能 / 覆盖率基准推迟**，无触发条件。
- **`IoStatus::interrupted` 有意永不返回**：EINTR 在读写循环内原地重试
  （`src/server/core/connections.cpp`）。
- **读侧不做背压节流**：没有「写积压时撤 `EPOLLIN`、排空后重挂」的机制，是有意选择。
- **`Connections` → `Connection` 改名**推迟，触发点 = 分帧状态机开工。
- **`io_status.h` 的 `to_string` 返回 PascalCase**，与 snake_case 枚举名不一致，推迟，触发点同上。
- **`run()` 用硬编码 `if (fd == listen_fd_)` 区分监听 fd**（`src/server/core/core.cpp:86`）。引入
  Channel / EventHandler 抽象的触发点 = **出现第二种 fd**（第二个 listener、timerfd 或 signalfd），
  在那之前不动。
- **`src/utils/` 是空目录**，与三层架构层名不对应，处置待定。
