# 心跳机制 — 原理、实现与面试考点

> 基于 PulseQt T018 实战，覆盖协议设计、代码实现、跨场景应用、面试八股。

---

## 一、为什么需要心跳

TCP 连接的"假活"问题：

```
客户端 ──── TCP 连接 ──── 服务端
            ↑
    双方都认为连接还在，但中间网络已经断了

原因：TCP 没有应用层数据时，keepalive 默认 2 小时才探测一次
结果：断线后 2 小时内双方都感知不到 → 数据丢失、资源泄漏
```

心跳 = 应用层定期发空包，"我还活着，你还活着吗？"

---

## 二、PulseQt 心跳实现

### 协议帧定义

```
心跳请求帧（上位机→下位机）:
  A5 5A 00 02 [CRC16]
  头   长=0 类型

心跳应答帧（下位机→上位机）:
  A5 5A 00 03 [CRC16]
  头   长=0 类型
```

### 核心代码

**定时检查（ParseWorker::onHeartbeatCheck）**：

```cpp
void ParseWorker::onHeartbeatCheck()
{
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastDataTime;

    // 5s 无数据 → 发心跳
    if (elapsed >= 5000) {
        emit writeData(buildFrame(Frame::TYPE_HEARTBEAT));
        m_heartbeatMissed++;
    }

    // 连续 6 次无应答 (30s) → 超时
    if (m_heartbeatMissed >= 6) {
        m_heartbeatTimer->stop();
        qWarning() << "Heartbeat timeout (30s)";
    }
}
```

**收到数据时重置**：

```cpp
void ParseWorker::onRawDataReceived(const QByteArray &data)
{
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();  // 任何数据都算"活动"
    m_heartbeatMissed = 0;
    m_decoder.feed(data);
}
```

**收到心跳应答时重置计数器**：

```cpp
// frameDecoded lambda 中
if (frame.type == Frame::TYPE_ACK) {
    m_heartbeatMissed = 0;
    return;
}
```

**收到对方心跳时回应**：

```cpp
if (frame.type == Frame::TYPE_HEARTBEAT) {
    emit writeData(buildFrame(Frame::TYPE_ACK));
    return;
}
```

### 状态机

```
        收到任意数据
  ┌─────────────────────────────┐
  │                             │
  ▼                             │
 [正常] ──5s无数据──→ [发心跳] ──→ missed++
  │                    │
  │        收到ACK      │ missed < 6
  └──────────────────────┘
  │
  │ missed >= 6 (30s)
  ▼
 [超时] → 停止心跳 → 上层断线重连
```

---

## 三、三种常见心跳模式

### 模式 1：Ping-Pong（PulseQt 采用）

```
A ── Ping ──→ B
A ←─ Pong ── B
```

| 优点 | 缺点 |
|------|------|
| 双向确认，A 知道自己发出的探测 B 收到了 | 需要 B 配合实现应答 |

**适用**：自定义协议、物联网设备、长连接服务器。

### 模式 2：单向心跳

```
A ── Ping ──→ B  （B 不回应）
A 只管发，B 只管收。B 端有超时检测。
```

| 优点 | 缺点 |
|------|------|
| B 不需要实现应答逻辑 | A 不知道 B 是否还活着 |

**适用**：单向数据上报（传感器→网关）、卫星遥测。

### 模式 3：TCP Keepalive（操作系统级）

```cpp
int keepalive = 1;
int idle = 60;      // 60s 无数据开始探测
int interval = 10;  // 每 10s 探测一次
int count = 3;      // 3 次失败判定断开
setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
```

| 优点 | 缺点 |
|------|------|
| 零应用层代码 | 全局生效，不能按连接定制 |
| 操作系统处理 | 默认 2 小时，改配置影响所有连接 |

---

## 四、设计心跳的三个关键参数

| 参数 | PulseQt 值 | 选择理由 |
|------|:--:|------|
| 心跳间隔 | 5s | 100Hz 数据流下 5s 已经是"空闲"了 |
| 超时阈值 | 30s（6×5s） | 网络抖动 2-3 个包正常，6 个确认断线 |
| 重置条件 | **收到任何数据**都重置 | 心跳目的是检测"对方还活着"，有数据就是活着 |

### 常见参数经验值

| 场景 | 心跳间隔 | 超时 | 说明 |
|------|:--:|:--:|------|
| 物联网设备 | 30s-60s | 3-5 次 | 省电，不频繁唤醒 |
| WebSocket | 30s | 3 次 | Nginx 默认 60s 超时 |
| 游戏长连接 | 5s-10s | 2-3 次 | 实时性要求高 |
| 金融行情 | 1s | 2 次 | 毫秒级感知断线 |
| 工业采集 | 5s | 5-6 次 | 平衡可靠性和带宽 |

---

## 五、面试常考问题

### Q1：TCP 有 keepalive，为什么还要应用层心跳？

```
TCP keepalive:
  - 默认 2 小时才探测（Linux）
  - 改配置影响全局所有连接
  - 只能检测网络层通断，不检测应用层假死

应用层心跳:
  - 间隔可控（5s vs 2h）
  - 可按连接定制策略
  - 能检测"服务进程卡死但 TCP 还连着"的情况
```

### Q2：心跳间隔设多大合适？

**没有固定值，看业务**：带宽敏感的物联网设备（NB-IoT）可能 30 分钟一次；金融行情 1 秒一次。核心公式：

```
心跳间隔 = 业务可容忍的最大断线感知时间 / 超时次数
```

PulseQt：业务要求 30s 内感知断线 → 30s / 6 次 = 5s 间隔。

### Q3：心跳包应该带数据吗？

```cpp
// 方案 A：纯心跳（PulseQt 采用）
// 帧 = Header + Length=0 + Type=0x02 + CRC

// 方案 B：带时间戳的心跳
// 可以计算网络延迟：收到应答的时间 - 请求中携带的发送时间 = RTT
```

一般用纯心跳。带宽充裕时可以带时间戳，方便监控链路质量。

### Q4：心跳处理和业务逻辑应该在同一线程吗？

```
PulseQt 的做法（✅ 正确）:
  解析线程：处理心跳（在 frameDecoded lambda 中判断 type=0x02/0x03）
  → 心跳是协议层的事，和数据帧同层处理

反例（❌ 不好）:
  单独线程处理心跳 → 和业务数据线程抢锁 → 复杂度翻倍
```

### Q5：如何防止心跳风暴？

```
场景：1000 台设备同时连一个服务器
如果所有设备同一时刻发心跳 → 服务器瞬间 1000 个包

解法：心跳间隔加随机抖动
  interval = 5000 + rand() % 1000;  // 5s ± 0.5s 随机
```

### Q6：收到心跳应答后要不要重置数据超时？

**要**。心跳应答也是"对方还活着的证据"。PulseQt 的 `m_lastDataTime` 在 `onRawDataReceived` 中更新——但心跳应答走的是 `frameDecoded` 而不是 `rawDataReceived`。

PulseQt 的处理：心跳应答在 frameDecoded 中只重置 `m_heartbeatMissed`，不重置 `m_lastDataTime`。这意味着心跳应答只能证明对方活着，但不能阻止继续发心跳——因为"没有业务数据"这个事实没变。

这是一个**合理的设计选择**，面试时可以讲出这个权衡。

---

## 六、PulseQt 实现中的反向通道

心跳需要上位机主动发数据，但 T013 的数据流是单向的（设备→上位机）。解决方案：加一条反向信号线。

```
ParseWorker                  TcpWorker                 TCP
    │                           │                      │
    │ emit writeData(frame)     │                      │
    │──────────────────────────→│ write(frame)         │
    │   (QueuedConnection)      │──────────────────────→│ 设备
```

`ParseWorker::writeData` 信号 → `TcpWorker::write` 槽 → `m_socket->write()`。不需要修改线程模型，信号槽自动跨线程。
