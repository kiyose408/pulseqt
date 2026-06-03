# PulseQt 数据流与信号传递 — 设计原理笔记

> T009（DataTableModel + QTableView）完成后复盘。解释从传感器到屏幕的数据如何一步步流转。

---

## 一、数据全链路（端到端）

```
┌──────────┐   TCP    ┌─────────────┐  readyRead ┌─────────────┐  feed(bytes) ┌────────────────┐
│ Python    │ ──────→ │ TcpChannel  │ ─────────→ │ ChannelMgr  │ ────────────→ │ ProtocolDecoder │
│ 模拟器    │  数据帧  │ (T003)      │  信号转发   │ (T004)      │  信号转发      │ (T005)          │
└──────────┘         └─────────────┘            └─────────────┘               └───────┬────────┘
                                                                                       │
                                                                              frameDecoded(Frame)
                                                                                       │
                                                                                       ▼
                                                                              ┌────────────────┐
                                                                              │  main.cpp      │
                                                                              │  lambda 解析    │
                                                                              │  Frame→DataPoint│
                                                                              └───────┬────────┘
                                                                                      │ push()
                                                                                      ▼
                                                                              ┌────────────────┐
                                                                              │  DataBuffer    │
                                                                              │  (T007)        │
                                                                              │  环形缓冲       │
                                                                              └───┬───────┬────┘
                                                                      bufferUpdated │      │ snapshot()
                                                                                    │      │
                                                                                    ▼      ▼
                                                                              ┌────────────────┐
                                                                              │ DataTableModel │
                                                                              │ (T009)         │
                                                                              └───────┬────────┘
                                                                              dataRefreshed
                                                                                      │
                                                                                      ▼
                                                                              ┌────────────────┐
                                                                              │  MainWindow    │
                                                                              │  scrollToBottom│
                                                                              └────────────────┘
```

---

## 二、三种信号角色的区别

| 信号 | 来源 | 目的地 | 携带数据 | 设计意图 |
|------|------|------|------|------|
| `readyRead(QByteArray)` | ChannelManager | ProtocolDecoder | 原始字节 | **传输层**：字节流，不知道含义 |
| `frameDecoded(Frame)` | ProtocolDecoder | 外部 lambda | 完整帧 | **协议层**：结构化数据，type+payload |
| `bufferUpdated(int)` | DataBuffer | DataTableModel | 数量通知 | **数据层**：告诉观察者"有新数据"，不传数据体 |
| `dataRefreshed()` | DataTableModel | MainWindow | 无数据 | **UI 层**：告诉视图"刷新完了，该滚动" |

**核心设计决策**：`bufferUpdated` 只传 `count`（新增条数），不传数据体。

```
❌ 如果传数据：
  signal bufferUpdated(QVector<DataPoint> data);
  → 信号连接时参数被拷贝一次
  → 100Hz × 10000 条 × 每次一个 QVector = 每秒 100 万次对象拷贝
  → UI 线程卡死

✅ 只传通知：
  signal bufferUpdated(int count);
  → 不拷贝数据
  → UI 线程自己调 snapshot() 拿一次
  → 控制在自己手里
```

---

## 三、Qt::QueuedConnection 为什么在这里是必须的

```cpp
void DataBuffer::push(const DataPoint &point)
{
    QMutexLocker locker(&m_mutex);   // 🔒
    m_ring[m_head] = point;
    // ...
    emit bufferUpdated(1);           // 信号在此发射
}                                    // 🔓 锁释放

void DataTableModel::onBufferUpdated(int count)
{
    auto snap = m_buffer->snapshot();  // 内部 QMutexLocker → 🔒
}
```

同线程下 `AutoConnection` = `DirectConnection`，`onBufferUpdated` 在 `emit` 点**当场执行**。此时 `push()` 还没释放锁 → `snapshot()` 再锁 → 同一个线程同一把 `QMutex` 锁两次 → **死锁**。

```cpp
// 修复：强制走事件队列
connect(buffer, &DataBuffer::bufferUpdated,
        model, &DataTableModel::onBufferUpdated,
        Qt::QueuedConnection);
//               ↑ push() 返回后，事件循环再调 onBufferUpdated
```

---

## 四、数据副本模式 — 为什么 snapshot() 不返回引用

```cpp
// ❌ 返回引用
const QVector<DataPoint>& snapshot() {
    QMutexLocker lock(&m_mutex);
    return m_data;   // 返回引用 → 锁释放 → 引用悬空！
}

// ✅ 返回副本
QVector<DataPoint> snapshot() {
    QMutexLocker lock(&m_mutex);
    QVector<DataPoint> copy = m_data;   // 锁内拷贝
    return copy;                         // 锁释放，返回独立副本
}
```

核心原则：**持有锁的时间内只做最少的事**——拷贝一份然后释放锁。调用方拿到返回后不再依赖原始数据，不需要考虑锁。

---

## 五、DataTableModel 的设计模式：视图/模型分离

```
┌──────────────┐      ┌─────────────────┐      ┌──────────┐
│  DataBuffer  │ ───→ │ DataTableModel  │ ───→ │QTableView│
│  (真实数据)   │      │ (数据的抽象视图)  │      │ (渲染)    │
└──────────────┘      └─────────────────┘      └──────────┘
     数据源               问答协议                    画像素

QTableView 不直接碰 DataBuffer，只通过 4 个约定好的"问题"和模型对话：
  - "你有几行？"         → rowCount()
  - "你有几列？"         → columnCount()
  - "第 (r,c) 格显示啥？" → data(r, c, DisplayRole)
  - "第 c 列标题是啥？"   → headerData(c)
```

带来的好处：

| 场景 | 好处 |
|------|------|
| 数据源换成数据库 | 只改 DataTableModel，QTableView 不动 |
| 换 UI 框架 | 只改 QTableView，DataTableModel 可以复用 |
| 单元测试 | 模型可以用 mock 数据源测，不需要真实 QTableView |

---

## 六、T009 踩到的坑

| # | 坑 | 原因 | 教训 |
|:--:|------|------|------|
| 1 | `rowConut` vs `rowCount` | 拼写 → 不是 override → 编译器报"Only virtual member" | 纯虚函数签名一个字都不能错 |
| 2 | 构造函数 LNK2019 | 头文件声明了但 cpp 没写实现 | 声明和定义要一起检查 |
| 3 | 表格无滚动 | 忘了 `scrollToBottom()` | 数据流通了 ≠ UI 交互通了 |
| 4 | `QueuedConnection` 差点忘 | 不用会死锁 | DataBuffer 有锁 → 信号槽必须队列连接 |

---

## 七、一条信号走过的完整路径（时间线）

```
T=0ms    模拟器发送二进制帧
T=0ms    QTcpSocket::readyRead
T=0ms    TcpChannel 读取字节 → emit readyRead(data)
T=0ms    ChannelManager::onReadyRead → emit readyRead(data)
T=0ms    ProtocolDecoder::feed(data)
T=0ms    状态机逐字节解析 → CRC 通过 → emit frameDecoded(frame)
T=0ms    lambda: Frame → DataPoint → DataBuffer::push()
T=0ms      push 持锁 → 写入环形数组 → emit bufferUpdated(1) → push 返回
T=0ms      锁释放
T=1ms    事件循环: onBufferUpdated → snapshot() → beginResetModel/endResetModel
T=2ms    QTableView 调用 rowCount/columnCount/data/headerData 重绘
T=2ms    emit dataRefreshed → scrollToBottom()
T=10ms   下一帧到达，循环
```

全程单线程（目前），每帧处理时间 < 2ms，100Hz 游刃有余。
