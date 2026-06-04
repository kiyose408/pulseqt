# Qt 多线程开发 — 基于 QThread + moveToThread

> 基于 PulseQt T013（Worker+QThread 三线程改造）实战总结。

---

## 一、为什么需要多线程

```
单线程（T012 之前）:
  主线程: UI绘制 → TCP收数据 → 协议解码 → 写DB → UI刷新
           ↑                          ↑
      用户操作流畅                  DB写入(1-5ms)卡UI

多线程（T013）:
  通信线程: TCP收数据                 ← 只做IO
     ↓ QueuedConnection
  解析线程: 解码 → 写DB               ← 只做计算+磁盘
     ↓ QueuedConnection
  主线程:   UI绘制 + 用户交互          ← 永不阻塞
```

核心原则：**哪个活会阻塞，就把它踢出主线程**。

---

## 二、三种线程方案对比

| 方案 | 复杂度 | 适用场景 |
|------|:--:|------|
| `QThread::run()` 重写 | 低 | 一次性后台任务（下载文件） |
| **`moveToThread`**（PulseQt 采用） | 中 | 长期存在的 Worker，通过信号槽通信 |
| `QtConcurrent::run` | 低 | 纯函数、无状态的计算 |

---

## 三、moveToThread 核心模式

### 固定套路（四步）

```cpp
// ① 创建 QThread（主线程）
QThread *thread = new QThread;

// ② 创建 Worker（主线程）
//    ⚠️ 构造函数不 new QObject 子对象（QSerialPort 等）
Worker *worker = new Worker;   // 无 parent

// ③ 移入线程
worker->moveToThread(thread);

// ④ 启动
thread->start();
```

### 为什么 Worker 构造时不能 new QSerialPort

```cpp
// ❌ 错误 — 构造在主线程，m_serial 归属主线程
Worker::Worker() {
    m_serial = new QSerialPort;   // 线程亲和性 = 主线程
}
worker->moveToThread(thread);
// m_serial 还在主线程！后面在通信线程里操作它 → 报错

// ✅ 正确 — open 在通信线程里执行，m_serial 归属通信线程
void Worker::open() {            // 这个槽在通信线程里执行
    m_serial = new QSerialPort;  // 线程亲和性 = 通信线程
}
```

**规则**：QObject 的子对象必须在其工作线程中创建。方法：把创建延迟到第一个槽中执行。

---

## 四、跨线程通信 — QueuedConnection

### 为什么必须用

```cpp
// AutoConnection（默认）: 同线程=直接调，跨线程=排队调
connect(worker, &Worker::rawData, parser, &Parser::onData);  // 可以

// 显式指定 QueuedConnection（推荐，意图清晰）
connect(worker, &Worker::rawData, parser, &Parser::onData,
        Qt::QueuedConnection);
```

### 信号参数怎么传递

```cpp
// 跨线程 signal → slot 时，参数自动深拷贝到接收线程
// 支持的类型：基础类型、QString、QByteArray、QVector 等 Qt 类型
// ⚠️ 不支持：裸指针、非 Qt 类型的引用
```

### invokeMethod — 手动触发跨线程调用

```cpp
// 让 Worker 在它自己的线程里执行 open()
QMetaObject::invokeMethod(worker, "open", Qt::QueuedConnection);

// 带参数
QMetaObject::invokeMethod(worker, "setName", Qt::QueuedConnection,
                          Q_ARG(QString, "hello"));
```

| `invokeMethod` vs `connect+emit` | 场景 |
|------|------|
| `invokeMethod` | 一次性调用（"去开一下串口"） |
| `connect+emit` | 持续事件流（"每次收到数据就通知我"） |

---

## 五、线程生命周期

### 安全退出四步（顺序不能错）

```cpp
void cleanup()
{
    // ① 关闭硬件（在 Worker 自己的线程里关）
    QMetaObject::invokeMethod(worker, "close", Qt::QueuedConnection);

    // ② Worker 在自己的线程里自杀
    worker->deleteLater();

    // ③ 退出事件循环
    thread->quit();

    // ④ 等线程结束
    thread->wait();   // 阻塞，等到线程真正退出

    worker = nullptr;
    delete thread;
}
```

### 为什么不用 terminate()

```cpp
thread->terminate();  // ❌ 暴力杀线程 — 锁没释放、内存没回收、DB 没 flush
thread->quit();       // ✅ 优雅退出 — 等事件循环自然结束
```

### 窗口关闭时的处理

```cpp
void MainWindow::closeEvent(QCloseEvent *event)
{
    onDisconnect();    // 先清理线程
    event->accept();   // 再关窗口
}
```

不写 `closeEvent` = 窗口先析构 → QThread 还在跑 → 报 "Destroyed while still running"。

---

## 六、QMutex — 数据共享

parseWorker 和 UI 线程都访问同一个 DataBuffer：

```cpp
// ParseWorker（解析线程）
void onFrame(const Frame &f) {
    m_buffer.push(dp);   // 加锁写入
}

// RealTimeChart（UI 线程）
void paintEvent() {
    auto snap = m_buffer->snapshot();  // 加锁拷贝
}
```

### DataBuffer 内部的线程安全保证

```cpp
// push() — 可能被解析线程调用
void DataBuffer::push(const DataPoint &dp) {
    QMutexLocker lock(&m_mutex);   // ① 加锁
    m_ring[m_head] = dp;
    m_head = (m_head + 1) % m_maxSize;
    emit bufferUpdated(1);          // ② 发信号
}                                    // ③ 锁释放

// snapshot() — 可能被 UI 线程调用
QVector<DataPoint> snapshot() const {
    QMutexLocker lock(&m_mutex);   // 加锁 → 拷贝 → 解锁 → 返回副本
}
```

### 死锁预防

```cpp
// ⚠️ 同线程 + signal/slot = 可能死锁
connect(&m_buffer, &DataBuffer::bufferUpdated,
        this, &DataTableModel::onUpdate,
        Qt::QueuedConnection);   // ← 必须 QueuedConnection！
```

同线程下 `DirectConnection` = 当场调用槽 → 槽里又调 `snapshot()` → 同一把锁再加 → 死锁。用 `QueuedConnection` → 锁释放后事件循环再调槽。

---

## 七、moveToThread 常见坑

| 坑 | 现象 | 原因 | 规则 |
|------|------|------|------|
| ① | `Cannot move to target thread` | Worker 有 parent | moveToThread 前不能设 parent |
| ② | `SendEvent` 断言失败 | 主线程直接调了 `worker->close()` | 必须用 `invokeMethod` |
| ③ | `Destroyed while running` | 窗口关了线程还在跑 | 加 `closeEvent` 清理 |
| ④ | Worker 槽不执行 | 线程没 `start()` 或 Worker 没 `moveToThread` | 检查顺序 |
| ⑤ | 信号收不到 | 跨线程但没用 `QueuedConnection` | 显式加第五个参数 |
| ⑥ | 数据竞争 | 两个线程同时写同一变量 | 加 QMutex 或用信号槽传值 |

---

## 八、PulseQt 线程架构

```
┌─────────────────────────────────────────┐
│                 主线程                    │
│  MainWindow                              │
│  ├── QTableView ← DataTableModel         │
│  ├── RealTimeChart ← 25FPS 定时器        │
│  └── snapshot() → DataBuffer (QMutex)    │
│        ↑ QueuedConnection                │
├─────────────────────────────────────────┤
│               解析线程                    │
│  ParseWorker                             │
│  ├── ProtocolDecoder (状态机)            │
│  ├── DataBuffer (环形缓冲, QMutex)      │
│  └── DatabaseManager (SQLite WAL)       │
│        ↑ QueuedConnection                │
├─────────────────────────────────────────┤
│               通信线程                    │
│  TcpWorker / SerialWorker               │
│  ├── QTcpSocket / QSerialPort           │
│  └── readyRead → rawDataReceived        │
└─────────────────────────────────────────┘
```

| 线程 | 做什么 | 不做什么 |
|------|------|------|
| 主线程 | UI 绘制、用户交互 | 不碰 IO、不做解码 |
| 解析线程 | 解码+DB写入+缓冲 | 不碰 UI |
| 通信线程 | 收发字节 | 不关心数据含义 |

---

## 九、什么时候用多线程

| 场景 | 是否需要 | 方案 |
|------|:--:|------|
| 数据库写入 | ✅ | 踢到解析线程 |
| TCP/串口收发 | ✅ | 通信线程 |
| 文件读写 | ✅ | worker 线程 |
| 复杂计算(>50ms) | ✅ | worker 线程 |
| UI 绘制 | ❌ | 只能在主线程 |
| 简单赋值/标记 | ❌ | 主线程就行，加线程反而复杂 |

**判断标准**：这个操作会阻塞超过一帧(16ms)吗？会 → 开线程。不会 → 主线程。
