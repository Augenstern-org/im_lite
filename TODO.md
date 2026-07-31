# com_lite TODO

> 本文件是本仓库**唯一**的开放事项清单。新的待办一律记在这里，不要再往
> `docs/architecture.md §7` 或 `docs/logs/*.md` 的「已知未收敛点」里各记一份 ——
> 那两处是历史快照，只读。锚点格式 `file:line`，以代码为准。

### 待办

- [ ] **跨 fd 转发（最高优先级，牵涉核心层底层实现）** `src/server/core/core.cpp:183`
  `handle_client()` 把 `send_queue` 里**每一条**出站帧都写给 `conn` —— 当前正在被服务的那条连接，
  `send_queue[i].second`（目标 fd）自始至终没有被读取。转发功能因此不存在；而且在它落地之前，
  上层**不得**产出指向其它 fd 的出站帧：那会让帧被**投错人**，比缺功能更糟。
  核心层需要新增「按 fd 取到 `Connections` 并驱动其写事件」的能力 —— `conns_` 在 `core.h:50` 是私有的、
  没有任何访问器，`Core` 的公开面只有 `init` / `run` / 两个 setter。
  - [ ] 目标 fd 的 `EPOLLOUT` 无人武装：`update_events()`（`core.h:42`，私有）在 `core.cpp:204`
    只对**当前** fd 调用一次。向另一个 fd 部分写入后，没有任何地方会给它挂上 `EPOLLOUT`，
    该连接的出站数据会**静默无限期滞留**。反方向同样要小心：LT 模式下 `EPOLLOUT` 挂在空闲可写的
    socket 上会让每次 `epoll_wait` 立刻返回，形成 CPU 空转。规则是「写缓冲非空才武装，排空即撤」。
  - [ ] 目标 fd 可能已在 `pending_close_` 里：`close_client()`（`core.cpp:224-228`）只做
    `EPOLL_CTL_DEL` + 入队，真正的 `erase` 推迟到 `drain_pending_close()`（`core.cpp:231-239`）。
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

- [ ] **核心层错误路径三连**（同一处循环的收尾，宜一并处理）
  - [ ] 解码状态被丢弃 `core.cpp:157`：`types::IoStatus dst = codec::Decoder::decode(in, rmp);`
    之后 `dst` 全文件再无引用。解码失败时 `rmp` 保持默认构造，仍被送进 `message_handler_`。
  - [ ] 回调未判空 `core.cpp:160`：`message_handler_` 未注册时抛 `std::bad_function_call`。
  - [ ] `in.clear()` 位置 `core.cpp:185`：在发送循环体内，随循环次数重复执行；
    语义上应在进入编码前一次性消费掉读缓冲。

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
  粘包 / 拆包的切分没有落点，`decode()` 仍假定调用方已交给它一个完整帧的边界。长度校验已在
  编解码两侧落地，仍缺「头部声明超长时如何丢弃后续字节并关闭连接」的连接级处置。
  - [ ] `decode()` 不报告消耗了多少字节，调用方无从推进流式读游标。
  - [ ] 错误分类过粗：除 `frame_too_long` 外全部返回 `error`，上层无法区分
    「协议错误、该杀连接」与「暂时不完整、该等更多字节」。

- [ ] **编码缓冲按条分配** `src/server/core/core.cpp:163-170`（原 TODO 注释在 `core.cpp:166`）
  每条出站消息都分配 65536 字节并清零，与实际帧长无关。方向是把超长帧校验与有效性校验
  前移到装配器，从而按实际长度分配。

- [ ] **帧完整性校验**：magic + CRC16，尚未设计。

- [ ] **零拷贝编码路径**：依赖上一条先落地。

- [ ] **调试观测点缺失** `src/server/controller/controller.cpp`
  原先打印 `from_uid_` / `content_` 的两行已删，服务端现在对收到的消息没有任何输出，
  端到端联调只能靠客户端侧的回显判断。

- [ ] **`docs/architecture.md` 与代码漂移**
  - [ ] `:200` 的 handler 签名仍写 `std::vector<MessagePack>&`，实际是
    `std::vector<std::pair<MessagePack, int>>&`（`src/server/core/core.h:27-31`）。
  - [ ] `:203`「仍为回显」与 `:30` 的骨架清单已过时。
  - [ ] `:165` 引用的 `main.cpp` 行号（`:14` / `:19` / `:29`）与实际不符。
  - [ ] `§7 待定项`（`:221-226`）改为指向本文件，不再各自维护一份。

### 有意缺失 / 已划定的边界

以下**不是**待办，是已经做出的决定。要改动它们，得先推翻决定本身。

- **装配器不做路由。** `coordination::Assembler` 的签名里没有 fd，也不持有 `UsersGroup` ——
  它只负责把出站 `MessagePack` **装配**出来，「送到哪个 fd」由调用方决定。在跨 fd 转发
  （本文件第 1 条）落地之前，出站帧的目标 fd 恒等于入站 fd；因为装配器无法表达 fd，
  误投在类型上不可发生。
- **`Encoder` 不做有效性校验、不检查 `out_buf` 容量**（`src/common/encoder.h:73-74`）。
  出站数据由程序自己构造，视为可信；入站数据才是敌意的。编解码不对称是有意的（2026-07-27 决策）。
- **编码器内部缓冲 / 缓冲复用**（`src/common/encoder.h:30` 注释掉的 `buf_`）**明确推迟，无触发条件**。
  等到确实出现复用需求或性能证据再定夺，不为尚未出现的性能问题先行设计（2026-07-29 决策）。
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
