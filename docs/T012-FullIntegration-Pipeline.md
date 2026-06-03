# T012 全链路串联 — 业务逻辑与代码分析

> 里程碑②：首次实现"点连接 → 数据从网络到屏幕 + 数据库"的完整闭环。

---

## 一、数据全链路（端到端）

```
┌──────────┐   TCP     ┌─────────────┐  raw bytes  ┌────────────────┐
│ 模拟器    │ ────────→ │ TcpChannel  │ ──────────→ │ ProtocolDecoder │
│ 100Hz    │  小端二进制 │ (T003)      │             │ (T005)          │
└──────────┘           └─────────────┘             └───────┬────────┘
                                                           │
                                                  frameDecoded(Frame)
                                                           │
                                                           ▼
                                                  ┌────────────────┐
                                                  │ MainWindow     │
                                                  │ onFrameDecoded │
                                                  └───┬───────┬────┘
                                                      │       │
                                              push()  │       │ insert()
                                                      ▼       ▼
                                                  ┌──────┐ ┌──────────────┐
                                                  │Buffer│ │DatabaseManager│
                                                  │(内存)│ │(data.db)      │
                                                  └──┬───┘ └──────────────┘
                                                     │
                                          bufferUpdated (QueuedConnection)
                                                     │
                                    ┌────────────────┼────────────────┐
                                    ▼                ▼                ▼
                            DataTableModel    RealTimeChart       (后续扩展)
                            → QTableView      → 自绘曲线
```

---

## 二、按钮与状态机

```
                    ┌─────────────────────────────────┐
                    │           初始状态               │
                    │   m_channelMgr = nullptr        │
                    │   m_collecting  = false         │
                    └───────────┬─────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │    点 "连接"          │
                    │  → onConnect()        │
                    │  创建管道              │
                    │  TCP 连接中...         │
                    └───────────┬───────────┘
                                │ connected 信号
                    ┌───────────▼───────────┐
                    │    点 "开始"          │
                    │  → onStart()          │
                    │  m_collecting = true  │
                    │  数据流入 buffer + DB  │
                    │  状态: "采集中..."     │
                    └───────────┬───────────┘
                                │
              ┌─────────────────┼─────────────────┐
              │                 │                 │
    ┌─────────▼──────┐  ┌──────▼───────┐  ┌──────▼───────┐
    │   点 "停止"     │  │  点 "断开"    │  │  意外断线     │
    │ m_collecting   │  │ 销毁管道      │  │ ChannelManager│
    │ = false        │  │ buffer 保留   │  │ 自动重连      │
    │ TCP 不断       │  │ 状态: 已断开  │  │ 状态: 重连中  │
    │ 状态: 已暂停   │  │               │  │               │
    └─────────┬──────┘  └──────┬───────┘  └──────┬───────┘
              │                │                 │
              ▼                ▼                 ▼
        可再点"开始"     可再点"连接"       连上 → "已连接"
        恢复采集          重建管道          失败 → 退避重试
```

---

## 三、核心代码逐段解析

### onConnect() — 创建数据管道

```cpp
void MainWindow::onConnect()
{
    if (m_channelMgr) return;        // 防重入：已连则忽略
```

**为什么**：按钮可能被连点两次，不加判断会创建两套管道互相冲突。

```cpp
    if (!m_buffer) {                 // buffer 只创建一次
        m_buffer = new DataBuffer(10000, this);
        setDataBuffer(m_buffer);     // 注入曲线 + 表格
    }
```

**为什么复用**：断开后再连接，旧的历史数据不该丢。`m_buffer` 在 `onDisconnect` 中不删除。

```cpp
    m_decoder    = new ProtocolDecoder(this);
    m_channelMgr = new ChannelManager(this);
    m_dbMgr      = new DatabaseManager(this);
    m_dbMgr->init("data.db");

    TcpChannel *tcp = new TcpChannel("127.0.0.1", 9999, this);
    m_channelMgr->setChannel(tcp);
```

**parent = this**：所有对象以 MainWindow 为父。MainWindow 析构时 Qt 自动 delete 所有子对象——不会泄漏。

```cpp
    connect(m_channelMgr, &ChannelManager::readyRead,
            m_decoder, &ProtocolDecoder::feed);
    connect(m_decoder, &ProtocolDecoder::frameDecoded,
            this, &MainWindow::onFrameDecoded);
```

**两条信号线**：字节→解码器，帧→MainWindow。中间不需要传数据副本，Qt 信号槽参数类型匹配就行。

```cpp
    connect(m_channelMgr, &ChannelManager::connected,
            [this]() { m_statusLabel->setText("已连接"); });
    connect(m_channelMgr, &ChannelManager::disconnected, [this]() {
        if (m_collecting) m_statusLabel->setText("已断开(重连中)");
        else m_statusLabel->setText("已断开");
    });
```

**状态栏联动**：采集中断线显示"重连中"，暂停中断线只显示"已断开"。

---

### onDisconnect() — 销毁管道，保留数据

```cpp
void MainWindow::onDisconnect()
{
    m_collecting = false;           // ① 先停采集
    if (!m_channelMgr) return;

    if (IChannel *ch = m_channelMgr->channel())
        ch->close();                // ② 关 TCP（abort）

    delete m_channelMgr;            // ③ 删管道（顺序很重要）
    delete m_decoder;
    if (m_dbMgr) { m_dbMgr->flush(); delete m_dbMgr; }
    // m_buffer 不删 ← ④ 历史数据保留
}
```

**删除顺序为什么重要**：

```
ChannelManager 先删 → 断开内部信号 → Decoder 不再收到数据
Decoder 次删 → 断开 frameDecoded → 不再回调 MainWindow
DB 最后删 → flush 残留的 100 条缓冲 → 关 SQLite
```

如果先删 Decoder，ChannelManager 的 `readyRead` 信号还连着 Decoder 的槽→悬空指针→crash。

---

### onFrameDecoded() — 帧 → 数据点 → 双写

```cpp
void MainWindow::onFrameDecoded(const Frame &frame)
{
    if (!m_collecting) return;      // 暂停中，忽略数据
    if (frame.type != Frame::TYPE_DATA || frame.payload.size() < 6) return;
```

**双重过滤**：`m_collecting` 是用户意图（点停止），`type != DATA` 过滤心跳帧等非数据帧。两者职责不同。

```cpp
    DataPoint dp;
    dp.timestamp = QDateTime::currentMSecsSinceEpoch();
    auto d = reinterpret_cast<const uint8_t*>(frame.payload.constData());
    dp.channels = {
        double(d[0] | (d[1] << 8)),
        double(d[2] | (d[3] << 8)),
        double(d[4] | (d[5] << 8))
    };
```

**小端序解析**：`d[0] | (d[1] << 8)` — 低字节 + 高字节左移 8 位拼成 uint16_t，再转 double。和模拟器 `struct.pack('<HHH')` 对应。

```cpp
    m_buffer->push(dp);
    m_dbMgr->insert(dp);
}
```

**双写模式**：内存 buffer（UI 可视化）+ SQLite（持久化）。`insert` 内部缓冲满 100 条才 commit，不卡主线程。

---

## 四、关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| buffer 断连不删 | 复用 | 重连后历史数据不丢失 |
| DB 断连时 flush | 是 | 最后几十条缓冲数据不浪费 |
| 开始/停止只控制采集 | 不控 TCP | 停止后恢复即时生效，不需要重建连接 |
| parent 统一为 this | ✅ | Qt 父子树自动管理，析构不用逐个 delete |
| 防重入 | `if(m_channelMgr) return` | 连点"连接"不会创建双管道 |

---

## 五、本次遇到的坑

| # | 现象 | 根因 | 修复 |
|:--:|------|------|------|
| 1 | `onConnect` 连点两次 crash | 无双管道防护 | `if(m_channelMgr) return` |
| 2 | 断开后 UI 清空 | `setDataBuffer(nullptr)` 掐断了曲线数据源 | 去掉，buffer 保留 |
| 3 | 重连后历史数据消失 | 每次 new 新 buffer 覆盖旧 buffer | buffer 复用，只创建一次 |
| 4 | 断开后无法重连 | `delete m_channelMgr` 没关底层 TcpChannel | 先 `ch->close()` 再 delete |
| 5 | 断开/停止后状态混淆 | 没有采集状态标记 | `m_collecting` 标志 |
| 6 | 模拟器断连即退出 | 没捕获 `ConnectionAbortedError` | 脚本加异常捕获 + 循环 accept |
