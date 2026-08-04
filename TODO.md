# com_lite TODO

> 本文件是本仓库**唯一**的开放事项清单。新的待办一律记在这里，不要再往
> `docs/architecture.md §7` 或 `docs/logs/*.md` 的「已知未收敛点」里各记一份 ——
> 那两处是历史快照，只读。锚点格式 `file:line`，以代码为准。

### 待办

- [ ] **跨 fd 转发（最高优先级，牵涉核心层底层实现）** `src/server/core/core.cpp:175`
  `handle_client()` 把 `send_queue` 里**每一条**出站帧都写给 `conn` —— 当前正在被服务的那条连接，
  `item.second`（目标 fd）自始至终没有被读取。转发功能因此不存在；而且在它落地之前，
  上层**不得**产出指向其它 fd 的出站帧：那会让帧被**投错人**，比缺功能更糟。
  核心层需要新增「按 fd 取到 `Connections` 并驱动其写事件」的能力 —— `conns_` 在 `core.h:51` 是私有的、
  没有任何访问器，`Core` 的公开面只有 `init` / `run` / 两个 setter。
  - [ ] 目标 fd 的 `EPOLLOUT` 无人武装：`update_events()`（`core.h:43`，私有）在 `core.cpp:209`
    只对**当前** fd 调用一次。向另一个 fd 部分写入后，没有任何地方会给它挂上 `EPOLLOUT`，
    该连接的出站数据会**静默无限期滞留**。反方向同样要小心：LT 模式下 `EPOLLOUT` 挂在空闲可写的
    socket 上会让每次 `epoll_wait` 立刻返回，形成 CPU 空转。规则是「写缓冲非空才武装，排空即撤」。
  - [ ] 目标 fd 可能已在 `pending_close_` 里：`close_client()`（`core.cpp:229-233`）只做
    `EPOLL_CTL_DEL` + 入队，真正的 `erase` 推迟到 `drain_pending_close()`（`core.cpp:236-244`）。
    因此 `conns_.find(target_fd)` **仍能命中**一个已被摘出 epoll、即将销毁的连接。
    跨 fd 写入必须同时排除 `pending_close_` 中的 fd。
  - [ ] 打通后才谈得上路由接入：`to_uid_` → fd 集合的解析与 fan-out（协调层职责，
    见 `docs/architecture.md` §2.2）。装配器只装配、不路由，路由是独立的一份工作。

- [ ] **`UsersGroup` 没有接入点** `src/server/coordination/users_group.h:13-17`
  `UsersGroup` 只有一个私有 `std::unordered_map<uint32_t, User> group_`，零方法；
  `User`（`user.h:11-16`）只有一个私有 `std::vector<int> connections_`，没有 uid 字段、没有方法。
  - [ ] 键类型不匹配：`group_` 的键是 `uint32_t`，而 `Message::from_uid_` / `to_uid_`
    （`src/common/message.h:34-35`）是 `std::string`。二者必须先统一。
  - [ ] 断开时解绑没有落点：`main.cpp` 的 disconnect 回调只打印日志。`docs/architecture.md:167-176`
    要求断开即从用户组移除 fd，否则 fd 号被新连接复用后会**把消息发给错的人**。
  - [ ] `users_group.cpp` 不属于任何 CMake target（`src/server/CMakeLists.txt`）。一旦给
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
    不是任何约定的应答体（`src/server/coordination/assembler.cpp`）。
  - [ ] `request` 的响应帧当前把入站 `Message` 原样复制，只把 opcode 翻成 `response`。
    真正该带的是服务端消息序号 —— 即下一条。

- [ ] **`ResponseMsg::server_seq_` 的去向** `docs/architecture.md:225`
  随消息模型塌缩去掉，尚未确定以何种形式回归：`Message` 的可选字段，还是移进帧头。

- [ ] **登录 / 握手协议** `docs/architecture.md:223`
  uid ↔ fd 绑定发生在登录时，但登录指令的具体形态（认证方式、`msg_type_` 取值）尚未定义。
  这是 `UsersGroup` 能被填充的前提。

- [ ] **分帧状态机** `src/common/decoder.h:25`
  范围说明（2026-08-03 改写）：本条现在**只为流式增量解析（性能议题）存在**——不把整帧攒在
  `read_buf_` 里就能推进解析。与错误路径正确性无关：docs/logs/2026-07-31.md §4.1 已推翻
  「必须等分帧状态机给出『本次消耗了多少字节』的出参」的前提——ok 路径消耗量 =
  `FrameHeader::wire_size + out_rmp.fh_.body_len_` 今天可得，`core.cpp:182` 的 drain 循环已按
  精确 erase 推进。字节级切分、「暂时不完整 vs 协议错误」的区分（`incomplete`，decoder.h:32
  帧头未齐 / :55 帧体未齐，保留字节）、超长帧关连接处置（`frame_too_long` → fatal → 关连接，
  core.cpp:177/:213-217）均已随「核心层错误路径三连」落地，不再是本条的待办。

- [ ] **帧完整性校验**：magic + CRC16，尚未设计。

- [ ] **零拷贝编码路径** `src/common/encoder.h:44-48`、`:99`、`src/server/core/core.cpp:176`
  依赖项（原「编码缓冲按条分配」）已落地：`encode_buf_` 跨消息、跨连接复用并按需增长，
  每条消息 65536 字节的分配与清零已经消失。这条还剩的是**拷贝次数** —— 一条出站帧仍要经过
  三次搬运：`body_serializer()` 先 `dump()` 出一个 `std::string`，`body_writer()` 再 `memcpy`
  进 `out_buf`，`conn.send()` 又把 `[0, out_len)` 拷进 `write_buf_`。
  - [ ] 消除前两次要让序列化直接落进 `out_buf`。难点是顺序：帧体长度必须在写入之前已知，
    超长帧才拦得住（`encoder.h:83`）；流式写入会让「先算长度再写」的前提失效，
    `encoder.h:64-69` 那条「出错时 `out_buf` 一字节未动」的后置条件也要重新设计。
  - [ ] 消除第三次要让编码直接写进连接的写缓冲，那会把编码缓冲的生命周期与 `Connections`
    绑死，与「复用缓冲归调用方持有」的现有边界相冲，得连带重新划线。

- [ ] **调试观测点缺失** `src/server/controller/controller.cpp`
  原先打印 `from_uid_` / `content_` 的两行已删，服务端现在对收到的消息没有任何输出，
  端到端联调只能靠客户端侧的回显判断。

- [ ] **`docs/architecture.md` 与代码漂移**
  - [ ] `:200` 的 handler 签名仍写 `std::vector<MessagePack>&`，实际是
    `std::vector<std::pair<MessagePack, int>>&`（`src/server/core/core.h:28-32`）。
  - [ ] `:203`「仍为回显」与 `:30` 的骨架清单已过时。
  - [ ] `:165` 引用的 `main.cpp` 行号（`:14` / `:19` / `:29`）与实际不符。
  - [ ] `:224`「超长帧防护」给次序硬约束列了**两条**理由，其中一条已作废：原文写
    「`Encoder::encode()` 在序列化完成之后、**向 `out_buf` 写入之前**拒绝——这个次序是硬约束，
    写在 `memcpy` 之后等于越界已经发生，且会破坏 `encode()` 自述的『返回错误时 `out_buf`
    一字节未动』后置条件」。前一条（越界）已不再成立：`encode()` 现在自己按需增长 `out_buf`
    （`src/common/encoder.h:95-96`），调用方不再负责容量，写多少都不会越界。后一条
    （`out_buf` 一字节未动）仍然成立。次序硬约束本身**依然是硬约束**，只是现役理由变成
    「防 DoS 放大：先分配后校验等于让 6 字节头诱导一次 64KB 分配」与「`resize` 会抛
    `bad_alloc`，必须排在 `body_len_` 回写之前，失败路径才不留下半写状态」——
    两条都记在 `encoder.h:87-94` 的注释里。
  - [ ] `§7 待定项`（`:221-226`）改为指向本文件，不再各自维护一份。
    
- [ ] `UsersGroup` `注册fd` `删除fd` 改为多线程设计。
- [ ] `register_handler` 暂时不能正确地将 `fd` 添加到 `uid_to_fd_` / `fd_to_uid_` 两哈希表，因为 `accept_client` 阶段不能读取 `uid`。

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
  复用缓冲是 `core::Core::encode_buf_`（`src/server/core/core.h:56`），`encode()` 依旧是 static、
  只把出参按需增长（`src/common/encoder.h:95-96`，只增不减），编码器本身不持有任何状态。
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
  （`decoder.h:47-50`，长度恰为 `body_len`），但那次分配排在**两道**校验之后 ——
  `decoder.h:44` 的上限校验，与 `decoder.h:45` 的 `in_buf.size() - wire_size < body_len`
  （即声称的字节确已全部到达）。因此当前的放大比是 **1:1**：要让服务端分配 N 字节，
  对端必须真的把 N 字节发过来。这不是「零分配」，是「分配量被实际到达量卡住」。
  将来若为性能加 `reserve(body_len)`、或按 `body_len` 预留读缓冲，危险**不是**新增放大面，
  而是**绕过 `:45` 这道已经在挡的闸** —— `reserve` 只需 6 字节头就能触发，放大比会从 1:1
  直接跳到 6:65536。届时 `decoder.h:44` 的上限校验**必须**排在任何按 `body_len` 定尺寸的
  动作之前，且预留量不得超过已到达字节数。编码侧 `encoder.h:83`（校验）与 `:95-96`（resize）
  的先后顺序就是这条规则的现成范例。
- **离线消息暂存暂不实现**（`docs/architecture.md:226`）。目标 uid 无在线 fd 时的处理不落地，
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
