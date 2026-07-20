# 非阻塞 Socket + epoll 参考

本文记录 `core/connections.cpp` 与 `core/core.cpp` 中非阻塞 I/O 模型涉及的关键概念、系统调用返回码含义，以及对应的处理方式。

## 1. 阻塞 socket 与非阻塞 socket 的区别

| | 阻塞 | 非阻塞（本项目采用） |
|---|---|---|
| 并发模型 | 一线程一连接，或线程池 | 单线程 + `epoll` 事件循环（`core.cpp` `Core::run()`） |
| 何时读/写 | 调用时天然等到数据就绪 | 需依赖 `EPOLLIN`/`EPOLLOUT` 通知"现在读/写不会阻塞" |
| 读取策略 | 一次 `read()` 通常足够 | 需循环读到 `EAGAIN` 为止（level-triggered 下不会丢数据，但需读干净以减少事件轮次） |
| 写入策略 | 阻塞到写完，或对端过慢导致调用方长时间挂起 | 需自行维护写缓冲区（`write_buf_`/`write_pos_`）与高水位背压（`kWriteHighWater`），写不完时注册 `EPOLLOUT` 等待下次可写 |
| 单线程承载连接数 | 差，一个连接阻塞会拖住整个线程 | 好，无 fd 会真正占用线程 |

设置非阻塞的方式：`fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)`。listen fd（`core.cpp:54`）与每个 accept 出的 client fd（`core.cpp:111-112`）都需要单独设置。

## 2. 从阻塞模型迁移到非阻塞模型的改动点

1. **socket 创建后必须显式设置 `O_NONBLOCK`**：listen fd 与所有 client fd 都要设置，遗漏任意一个都会导致该 fd 在对应调用上退化为阻塞。
2. **`accept()` 需要循环处理**：epoll 通知的是"listen fd 上有活动"这一事件，不代表只有一个连接在排队。需要循环 `accept()` 直到返回 `EAGAIN`（见 `core.cpp:100-121`）。
3. **`read()` 需要循环读到 `EAGAIN`**：内核缓冲区中可能积压了比单次 `read()` 缓冲区更多的数据。level-triggered 模式下不循环读完不会丢数据（下次 `epoll_wait` 还会再通知），但会导致同一批数据被拆成多轮事件处理，增加不必要的 syscall（见 `connections.cpp:30-48`）。
4. **errno 需要分类处理**：`read()`/`send()` 返回负数时，需先区分是"伪错误"（`EINTR`/`EAGAIN`）还是真实错误，再决定处理方式（见第 3 节）。
5. **写路径需要缓冲区 + 续传机制**：`send()` 可能只写出部分字节甚至 0 字节，需要自行维护 `write_buf_`/`write_pos_`，未写完的部分留存到下次可写事件时继续发送（见 `connections.h:36-37`、`connections.cpp:71-97`）。
6. **`EPOLLOUT` 需要按需订阅**：多数时候 socket 处于可写状态，若持续订阅 `EPOLLOUT` 会导致 `epoll_wait` 不断被无意义唤醒（busy loop）。应仅在写缓冲区非空（`want_write()` 为真）时加入 `EPOLLOUT`，写完后需在下次重算掩码时移除（见 `core.cpp:172-186`）。
7. **需要处理半包/粘包**：非阻塞读取下，一条逻辑消息可能被拆分为多次 `read()` 才能收全，也可能一次 `read()` 中包含多条消息的数据。读缓冲区（`read_buf_`）需要跨多次事件持久化累积，交由上层协议解析器判断消息边界。当前实现仅做原样回显（`core.cpp:148-156`），尚未实现分帧逻辑。
8. **连接关闭需要分两步处理**：
   - 对端发送 `FIN`（`read()` 返回 0）不代表可立即关闭，写缓冲区中可能还有未发送完的数据，需等待 `flush()` 排空后再关闭（见 `core.cpp:163-166`）。
   - `epoll_ctl(EPOLL_CTL_DEL)` 必须先于 `close()`，且不能在事件处理循环内直接从 `conns_` 中 `erase`（同一轮 `epoll_wait` 返回的事件数组中可能还有该 fd 的其他待处理条目）。本项目使用 `pending_close_` 延迟队列，先摘除 epoll 注册并记录待关闭 fd，待本轮事件全部处理完毕后由 `drain_pending_close()` 统一清理（见 `core.cpp:188-202`）。

## 3. errno（系统调用失败原因）

以下均为 `read()`/`send()`/`accept()` 返回负数时 `errno` 的取值。

| errno | 触发条件 | 处理方式 |
|---|---|---|
| `EINTR` | 系统调用执行过程中进程收到信号被提前中断。与阻塞/非阻塞无关，阻塞 socket 同样会遇到。 | 立即重试（`continue`） |
| `EAGAIN` / `EWOULDBLOCK` | 非阻塞模式特有。读：接收缓冲区无数据；写：发送缓冲区已满；`accept()`：无排队连接。Linux 上二者数值相同。 | 不视为错误，退出当前循环，等待下一次 `epoll_wait` 通知（`IoStatus::would_block`） |
| `EPIPE` | 对端读端已关闭（收到过 RST 或已完全断开）后仍尝试写入。默认会同时触发 `SIGPIPE` 导致进程终止，需通过 `send()` 的 `MSG_NOSIGNAL` flag（见 `connections.cpp:76`）改为仅返回该 errno。 | 视为连接已关闭（`IoStatus::closed`） |
| `ECONNRESET` | 对端未走正常四次挥手，直接发送 RST（进程崩溃、`SO_LINGER` 强制重置、中间网络设备重置连接等）。 | 视为连接已关闭（`IoStatus::closed`） |

对应代码：`connections.cpp:43-47`（读路径）、`connections.cpp:84-92`（写路径）。

## 4. epoll 事件位

以下为 `epoll_wait()` 返回的 `events[i].events` 位掩码取值，表示"该 fd 现在可以尝试的操作"，具体调用结果仍需结合第 3 节的 errno 判断。

| 事件位 | 触发条件 | 是否需主动订阅 | 处理方式 |
|---|---|---|---|
| `EPOLLIN` | 接收缓冲区有数据；listen fd 上有新连接；对端发送 `FIN`（可读到 EOF 也视为可读） | 是 | 调用 `read()`，循环读到 `EAGAIN` |
| `EPOLLOUT` | 发送缓冲区有可用空间 | 是，且仅在 `want_write()` 为真时订阅，避免忙轮询 | 调用 `flush()` 续写写缓冲区 |
| `EPOLLHUP` | 对端读写两个方向均已关闭 | 否，内核无条件报送 | 直接关闭连接 |
| `EPOLLERR` | fd 出现底层错误（如异步 `connect()` 失败、`SO_ERROR` 被设置） | 否，内核无条件报送 | 直接关闭连接 |

对应代码：`core.cpp:130-133`（`EPOLLHUP`/`EPOLLERR`）、`core.cpp:136-142`（`EPOLLOUT`）、`core.cpp:144-167`（`EPOLLIN`）、`core.cpp:172-186`（掩码重算与 `EPOLLOUT` 按需订阅）。

## 5. 处理流程

1. `epoll_wait()` 返回 `EPOLLHUP`/`EPOLLERR` → 直接关闭连接，不再尝试 I/O。
2. 返回 `EPOLLIN` → 调用 `read()`：
   - 返回值 > 0：追加到读缓冲区，继续循环读取。
   - 返回值 == 0：对端发送 `FIN`，标记 `peer_closed_`。
   - 返回值 < 0：
     - `errno == EINTR` → 重试。
     - `errno == EAGAIN`/`EWOULDBLOCK` → 本轮数据已读完，退出循环，等待下次通知。
     - 其他 → 视为真实错误。
3. 返回 `EPOLLOUT` → 调用 `flush()` 续写写缓冲区，处理方式同上（`EAGAIN` 表示仍未写完，等待下次可写通知）。
4. 每次事件处理完毕后，根据连接当前状态（`peer_closed()`、`want_write()`）重新计算 `epoll_ctl` 掩码并调用 `EPOLL_CTL_MOD`（`EPOLL_CTL_MOD` 会整体替换掩码，需重算完整值而非增量修改）。

## 6. IoStatus 返回值契约

`types::IoStatus`（`common/io_status.h`）是核心层读写路径**唯一**的结果词汇。读写两个方向共用同一套语义，这样调用方可以用同一组分支处理二者。

| 取值 | 语义 | 调用方反应 |
|---|---|---|
| `ok` | 本次操作完成，**该方向无待办**。读：达到软上限主动停止；写：缓冲区已排空 | 保留连接，重算掩码 |
| `would_block` | 内核拒绝继续（`EAGAIN`）。读：已读空；写：仍有字节排队 | 保留连接，重算掩码 |
| `closed` | 有序终止。读：`read()` 返回 0；写：`EPIPE`/`ECONNRESET`；或半关闭排空后收尾 | 关闭连接 |
| `error` | 不可恢复：非预期 errno，或写缓冲越过高水位 | 关闭连接 |
| `interrupted` | **永不返回**。`EINTR` 在读写循环内原地重试（`connections.cpp:43`、`:84`） | — |
| `frame_too_long` / `timeout` | 当前阶段**永不返回**，保留给后续分帧/超时机制 | — |

关于 `interrupted`：这是一个**已定的设计决策，不是待办**。`EINTR` 表示"零字节已传输"，原地重试即可；若向上返回，调用方必须再实现一遍下一层已经写好的重试循环，属于重复逻辑。

关于 `ok` 与 `would_block` 的区分：在当前阶段二者对调用方的效果相同（都是"保留连接"）。保留区分是因为它在读路径上是诚实的——`ok` 表示"我主动停下了，内核里可能还有数据"，`would_block` 表示"确实读空了"。后续分帧逻辑需要这个差别。

## 7. 写路径的缓冲区管理

### 7.1 不变量

整个写路径依赖一条不变量：

```
write_pos_ <= write_buf_.size()
未发送字节数 == write_buf_.size() - write_pos_
```

`want_write()`（`connections.h`）即 `write_pos_ < write_buf_.size()`。`flush()` 只在循环条件成立时推进 `write_pos_`（`connections.cpp:72,78`），排空出口同时重置二者（`:94-95`），`compact()` 切除前缀时也同步归零（`:101-102`）——三处共同维持该不变量。高水位判断中的减法（`:64`）因此不会发生无符号下溢。

### 7.2 `flush()` 是唯一写原语

`send()` 与 `on_writeable()` 都不直接碰 socket，一律委托 `flush()`。这样 `MSG_NOSIGNAL`、短写续传、`EINTR` 重试、errno 分类这四件容易写错的事只存在一份实现。

`send()` 的顺序是**先 `append` 再 `flush()`**（`connections.cpp:67-68`），不可颠倒：这样新旧欠账在同一个循环里统一处理，调用方也能确信 `send()` 返回后字节已安全进入缓冲区（这是 `core.cpp:151` 那句 `in.clear()` 成立的前提）。

`flush()` 中 `n == 0` 的分支（`connections.cpp:81-83`）作用是**保护后续的 `errno` 读取**：`errno` 仅在系统调用返回负值时有意义，先挡掉 0 可使其后所有分支都处于 `n < 0` 的前提下。由于循环条件保证传入长度非零，`send()` 返回 0 本不应发生，按异常处理。

### 7.3 压缩策略

长期活跃的连接若反复发生短写，`write_pos_` 会持续前移，已发送的前缀却一直占用内存。`compact()`（`connections.cpp:99-104`）按两个条件切除前缀：

- `write_pos_ > kCompactThreshold`（8 KiB）——消费量足够大才值得搬移；
- `write_pos_ * 2 > write_buf_.size()`——消费量过半才搬移，使 `memmove` 成本摊薄为每字节 O(1)。

`compact()` **仅在 `EAGAIN` 路径调用**（`connections.cpp:86`），因为那是唯一"未发完、需保留余量"的情形；完全排空的路径直接 `clear()`，无需搬移。`clear()` 不释放容量，因此稳态收发不产生内存分配。

### 7.4 高水位背压

`send()` 在追加前检查总欠账是否会超过 `kWriteHighWater`（1 MiB），超过则返回 `error`（`connections.cpp:64-66`）。

这不是可选优化：若对端持续发送却从不读取，回显数据无法送出，`write_buf_` 将只增不减直至耗尽内存——这是一条无成本的内存耗尽攻击路径。当前策略是直接断开该连接（"背压杀连接"）。

**已知取舍**：读侧当前不做节流，即没有"写积压时撤销 `EPOLLIN`、排空后重新挂载"的机制。这是有意选择而非遗漏；代价是慢速读取方会被断开而不是被减速。

### 7.5 读侧软上限

`on_readable()` 在 `read_buf_` 达到 `kReadSoftCap`（64 KiB）时主动停止并返回 `ok`（`connections.cpp:34-36`）。

**该做法的正确性完全依赖 level-triggered**：提前退出后余下字节仍留在内核接收缓冲区，下一轮 `epoll_wait` 会再次报告该 fd 可读，因此不丢数据。**若改用 edge-triggered，此处会永久丢失数据**——ET 只在状态变化时通知一次，不读干净就再也不会被唤醒。

此上限是**缓冲区软上限，不是帧长检查**，因此不使用 `frame_too_long`——后者的语义保留给后续分帧逻辑中的 `MAX_BODY_LEN` 校验，提前借用会污染该语义。

## 8. 事件处理中不可调换的顺序

`Core::handle_client`（`core.cpp:123-170`）内有三处顺序具有约束性，调换会引入缺陷。

**（1）`EPOLLHUP`/`EPOLLERR` 分支最先**（`core.cpp:130-133`）。连接已处于错误状态，继续 I/O 无意义。

> 已知缺陷：该分支在读取之前短路返回，对端以 RST 断开时，内核接收缓冲区中的在途字节及 `read_buf_` 已累积的内容会被直接丢弃、不回显。收紧做法是注册 `EPOLLRDHUP` 并与 `EPOLLHUP` 分开处理。

**（2）`EPOLLOUT` 先于 `EPOLLIN`**（`core.cpp:136` 先于 `:144`）。先排空写缓冲可为随后新读入的字节让出高水位余量；顺序颠倒会使新数据更容易撞上 1 MiB 上限而导致连接被误杀。

**（3）回显先于读状态分派**（`core.cpp:148-156` 先于 `:158-166`）。`on_readable()` 可能在**同一次调用**中既向 `read_buf_` 追加数据、又返回 `closed`——对端将数据与 `FIN` 一并发送时即是如此。若先依据 `rst == closed` 关闭连接，这批已读入的数据会被静默丢弃。

**（4）`closed` 且仍有欠账时不关闭**（`core.cpp:163-166`）。对端发送 `FIN` 仅表示其关闭了写方向，通常仍在读取回显。此时保留连接，由 `update_events` 撤销 `EPOLLIN` 并保留 `EPOLLOUT`，待 `on_writeable()` 排空后以 `::shutdown(fd_, SHUT_WR)` 收尾并返回 `closed`（`connections.cpp:53-56`）。

该半关闭状态机完全封装在 `Connections` 内部，因此 `Core` 无需引入 `closing` 中间状态——它只在真正应当拆除的时刻收到 `closed`。

## 9. 连接生命周期与 fd 号回收

`epoll_wait()` 返回的是一个**事件快照数组**。在处理该数组的过程中关闭连接，会同时引入三类缺陷：

1. **对象自毁**：`handle_client` 的栈帧中持有 `Connections& conn`（`core.cpp:128`）。若在处理过程中 `conns_.erase(fd)`，该引用立即悬空，后续访问为 use-after-free。
2. **同批陈旧事件**：同一数组中靠后的条目可能仍指向该 fd（例如先 `EPOLLIN` 后 `EPOLLHUP`）。
3. **fd 号回收**（最隐蔽）：`close(fd)` 后内核可立即将该号分配出去。若同批事件中靠前的条目是 listen fd，`accept()` 会**当场取得同一个 fd 号**并注册进 `conns_`。此后属于旧连接的陈旧事件用该 fd 查表**会成功**——但查到的是一条身份完全不同的新连接。

第 3 点意味着**仅靠 `conns_.find(fd)` 判活是无效的**：`find` 会成功，只是结果不对。

本项目的解法是把关闭拆成两阶段：

- `close_client()`（`core.cpp:188-192`）只做 `EPOLL_CTL_DEL` 并将 fd 压入 `pending_close_`，**不调用 `close()`**；
- `run()` 在分派每个事件前线性扫描 `pending_close_` 并跳过其中的 fd（`core.cpp:85-91`），解决第 2 点；
- `drain_pending_close()`（`core.cpp:194-202`）在整批事件处理完毕后才 `conns_.erase(fd)`，由 `~Connections()` 执行 `close(fd_)`（`connections.cpp:22-26`），解决第 1 点；
- 因为真正的 `close()` 被推迟到批次之外，内核在本批次内始终未释放该 fd 号，`accept()` 不可能撞号，第 3 点从根本上不成立。

`pending_close_` 使用线性扫描而非哈希容器：其规模上界即单批事件数（本项目为 64），连续内存的线性扫描在此规模下优于哈希查找。

`disconnect_handler_` 在 `erase` **之后**触发，且 fd 按值传入（`core.cpp:196-199`），确保回调执行时不存在任何指向已析构对象的引用。核心层自身在拆除路径上不产生任何日志输出——可见性完全依赖上层是否装配了回调，这是"核心层只上报"的直接后果。

`EPOLL_CTL_DEL` 保持显式调用且早于 `close()`：epoll 注册的是**打开文件描述**而非 fd 号，`dup()`/`fork()` 会留下幻影注册，依赖 `close()` 自动摘除并不可靠。

## 10. level-triggered 下的两类忙轮询

LT 报告的是"当前状态满足条件"而非"状态发生变化"，因此任何**持续成立**的条件若被订阅而不处理，都会导致 `epoll_wait` 立即返回、形成 100% CPU 空转。本项目涉及两类：

**（1）无数据可写时订阅 `EPOLLOUT`。** socket 发送缓冲区通常有空位，即持续可写。解法是仅在 `want_write()` 为真时挂载 `EPOLLOUT`，排空后在下次掩码重算时移除（`core.cpp:175-177`）。

**（2）`read()` 返回 0 之后仍订阅 `EPOLLIN`。** 对端发送 `FIN` 后该 fd **永久保持可读**，每次 `read()` 都立即返回 0。因此 `peer_closed()` 为真时必须将 `EPOLLIN` 从掩码中撤除（`core.cpp:174`）。

第 2 类在半关闭场景中必然触发，且后果严重——Netty 曾因同一形状的缺陷可被远程利用为 DoS（PR #16689）。

掩码重算（`core.cpp:172-186`）每次都从连接状态计算完整值并无条件 `EPOLL_CTL_MOD`，不缓存"当前已注册的掩码"。原因是 `EPOLL_CTL_MOD` 为整体替换语义：若为添加 `EPOLLOUT` 而只传入 `EPOLLOUT`，`EPOLLIN` 会被静默移除，表现为"连接突然不再收数据"且无任何错误提示。每次全量重算使该类缺陷在结构上无法发生，代价是每个事件一次 `epoll_ctl` 调用。

> 注：`update_events` 中 `mask == 0` 的分支（`core.cpp:178-181`）当前**不可达**——该组合要求 `peer_closed() && !want_write()`，而这一情形已被 `core.cpp:163-166` 提前拦截并返回。属冗余防御代码；其风险在于若确实触发，会在无任何诊断输出的情况下关闭连接。

## 11. C++ 层面的陷阱

**成员函数 `send` 遮蔽全局 `::send`。** `Connections::send` 在类作用域内隐藏了同名的 `::send(2)`。类内所有写调用必须显式写作 `::send`（`connections.cpp:73`）；`::shutdown` 同理（`:54`）。二者签名不同（成员 2 参、全局 4 参），因此遗漏 `::` 表现为重载决议失败的编译错误，而非静默的错误调用。

**显式 `= delete` 拷贝构造会连带抑制隐式移动构造。** `Connections` 删除了拷贝构造（`connections.h:18`）且未声明移动构造，因此该类型**既不可拷贝也不可移动**。此类对象只能置于**节点式容器**中——`std::unordered_map` 的元素在 rehash 时不发生重定位，故 `conns_.try_emplace(client_fd, client_fd)`（`core.cpp:119`）可原地构造；而 `std::vector` 在扩容时需要移动元素，会在编译期失败。同理，`insert()` 与 `operator[]`（后者还要求默认构造）均不可用。

**`MSG_NOSIGNAL` 属于必需项而非优化。** 缺失时，向已关闭的对端写入会触发 `SIGPIPE`，其默认处置为**终止进程**——服务端整体消失且不留任何日志，症状表现为"无故静默退出"。加上该 flag 后内核改为返回 `EPIPE`，可按普通错误处理。

**析构中的资源释放需判空。** `~Connections()` 在 `close(fd_)` 前检查 `fd_ >= 0`（`connections.cpp:23`），因为 `fd_` 默认值为 `-1`。
