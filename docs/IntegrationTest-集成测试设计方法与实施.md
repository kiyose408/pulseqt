# QtTest 集成测试 — 设计与实施全解

> 基于 T025 全链路集成测试实战，覆盖 QProcess 子进程管理、多线程生命周期、TCP 端口就绪检测、CI 隔离策略。

---

## 一、集成测试 vs 单元测试：边界在哪

| 维度 | 单元测试 (T022–T024) | 集成测试 (T025) |
|------|:--|:--|
| 被测范围 | 单个类/模块 | 完整管道：TCP → Decoder → Buffer → SQLite |
| 外部依赖 | 零（纯内存） | Python 模拟器子进程 |
| 线程 | 单线程 / 手动多线程压测 | 真实三线程架构（comm + parse + main） |
| 数据验证 | 精确值对比 | 统计验证（≥ 900 条，值在范围） |
| 运行时间 | < 1s | ~37s |
| CI 策略 | 全跑 | 排除（`ctest -E Integration`） |

**核心原则**：集成测试不追求精确——验证"管道能跑通"即可，精确性留给单元测试。

---

## 二、T025 测试架构

```
TestIntegration
  │
  ├── dataCollection()
  │     ├── startSimulator()          QProcess → python tcp_wave_simulator.py
  │     ├── 端口轮询就绪               QTcpSocket::connectToHost 循环探测
  │     ├── 构建管道                   TcpWorker + ParseWorker + QThread × 2
  │     ├── 连接 + 采集 10s            QMetaObject::invokeMethod("open")
  │     ├── 断开 + flush
  │     ├── 验证 ①: DB rowCount ≥ 900
  │     ├── 验证 ②: 通道值 ∈ [0, 1023]
  │     └── stopThreads() + stopSimulator()
  │
  └── reconnection()
        ├── Round 1: 采集 → flush → 记录 n1
        ├── stopThreads() + stopSimulator()
        ├── Round 2: 重启模拟器 + 全新 TcpWorker/ParseWorker → 同一 DB 文件
        ├── 验证: DB rowCount > 0（重新连接后继续写入）
        └── stopThreads() + stopSimulator()
```

---

## 三、QProcess 子进程管理

### 3.1 启动模拟器

```cpp
bool startSimulator()
{
    killStaleSimulators();  // ① 清理上次残留

    m_simulator = new QProcess(this);
    m_simulator->setProcessChannelMode(QProcess::MergedChannels);

    QStringList args;
    args << "-u" << m_simScript;   // -u: Python unbuffered stdout
    m_simulator->start("python", args);

    if (!m_simulator->waitForStarted(5000))
        return false;

    // ② 端口轮询就绪（不等 stdout）
    for (int i = 0; i < 40; ++i) {
        QTcpSocket probe;
        probe.connectToHost("127.0.0.1", 9999);
        if (probe.waitForConnected(500)) {
            probe.disconnectFromHost();
            return true;
        }
        QTest::qWait(250);
    }
    return false;
}
```

**两个关键设计**：

| 设计 | 为什么 |
|------|------|
| `python -u` | 关闭 Python stdout 缓冲，`waitForReadyRead` 能立刻读到输出 |
| 端口轮询 | 比读 stdout 可靠——进程可能启动了但 `print()` 还没 flush |

### 3.2 清理残留进程

```cpp
void killStaleSimulators()
{
    QProcess killer;
    killer.start("taskkill", {"/f", "/im", "python.exe"});
    killer.waitForFinished(3000);
    QTest::qWait(500);  // 等端口释放
}
```

**为什么需要？** 上一个测试如果崩溃或 ctest 被 Ctrl+C 打断，Python 子进程可能还活着，占用 9999 端口。下一个测试 `startSimulator()` 时端口轮询永远连不上。

**平台注意**：`taskkill` 仅 Windows。Linux 需 `pkill -f tcp_wave_simulator`，macOS 需 `killall python`。

### 3.3 停止模拟器

```cpp
void stopSimulator()
{
    if (m_simulator && m_simulator->state() != QProcess::NotRunning) {
        m_simulator->terminate();          // SIGTERM / TerminateProcess
        m_simulator->waitForFinished(5000);
        killStaleSimulators();             // 兜底
    }
}
```

`terminate()` 后用 `waitForFinished()` 阻塞等进程退出，防止僵尸进程。

---

## 四、多线程生命周期管理

### 4.1 问题场景

集成测试创建了真实的三线程架构：

```
main thread  ←── Qt::QueuedConnection ──→  commThread (TcpWorker + QTcpSocket)
                                        →  parseThread (ParseWorker + QTimer)
```

当测试函数返回时，栈上的 `QThread` 对象析构。如果线程还在跑着 `QTcpSocket` 和 `QTimer`，析构发生在 main thread，而这些对象的内部组件（`QNativeSocketEngine`）的 EventDispatcher 在另一个线程里——跨线程析构直接触发 Qt 断言崩溃。

### 4.2 解决方案：BlockingQueuedConnection

```cpp
void stopThreads(QThread &comm, QThread &parse, TcpWorker &tcp, ParseWorker &parser)
{
    parser.setCollecting(false);

    // ★ 关键：在 comm 线程内执行 close，main 线程阻塞等待完成
    QMetaObject::invokeMethod(&tcp, "close", Qt::BlockingQueuedConnection);

    comm.quit();
    parse.quit();
    comm.wait(5000);
    parse.wait(5000);
}
```

**`Qt::BlockingQueuedConnection` 是集成测试里最重要的工具**：
- 它把 `close()` 的执行调度到 `tcp` 对象所在的 commThread
- main thread 阻塞等待 `close()` 在该线程内执行完毕
- 这样 `QTcpSocket` 的内部清理完全在它自己的线程里完成
- 之后 `quit() → wait()` 安全

### 4.3 为什么不在 `close()` 后直接 `quit()`?

```cpp
// ❌ 错误：QueuedConnection 不阻塞，close 还没执行就 quit 了
QMetaObject::invokeMethod(&tcp, "close", Qt::QueuedConnection);
comm.quit();
comm.wait();

// ✅ 正确：BlockingQueuedConnection 确保 close 执行完才继续
QMetaObject::invokeMethod(&tcp, "close", Qt::BlockingQueuedConnection);
comm.quit();
comm.wait();
```

---

## 五、数据验证策略

### 5.1 不验精确值，验统计量

集成测试不逐帧校验数据（单元测试 T022 已经做过），只验：

```cpp
// ① 数量合理（100Hz × 10s，允许 10% 丢帧）
QVERIFY(count >= 900);

// ② 值在物理范围内（正弦波 0-1023）
QVERIFY(ch0 >= 0.0 && ch0 <= 1023.0);
```

### 5.2 同一 DB 文件验证重连

```cpp
// Round 1 写入 DB
ParseWorker parser(db2);  // "test_pulseqt_t025b.db"
// ... 采集 2s → flush → n1 条

// Round 2: 新 TcpWorker + 新 ParseWorker，同一 DB 文件
ParseWorker parser(db2);
// 重新连接 → 采集 2s → flush → n2 条
QVERIFY(n2 > 0);  // 证明了断线后重新连接可以继续写入
```

**关键**：第二轮不是复用 TcpWorker（线程所有权问题），而是**全新构建管道**，只共享 DB 文件。

---

## 六、CI 隔离

### 6.1 为什么 CI 不跑集成测试

```yaml
- name: Test
  run: ctest --test-dir build -C Release --output-on-failure -E Integration
  #                                                       ^^^^^^^^^^^^^^^^
```

`-E Integration` 排除集成测试。原因：

| 问题 | 说明 |
|------|------|
| Python 依赖 | CI runner 可能没有 Python / pyserial |
| 端口可能冲突 | 多 job 并行时 9999 被占用 |
| 耗时 37s | CI 按分钟计费，集成测试占大部分时间 |
| `taskkill` 不可移植 | Linux CI 无此命令 |

### 6.2 本地 vs CI

| 环境 | 跑什么 |
|------|------|
| 本地 `ctest` | 全部 5 个测试（含 Integration） |
| CI `ctest` | 4 个单元测试（排除 Integration） |
| 本地手动 | `TestIntegration.exe -v1` 看详细输出 |

---

## 七、常见踩坑

### 7.1 端口残留

| 现象 | 原因 | 解决 |
|------|------|------|
| `startSimulator()` 端口轮询一直超时 | 上次测试崩溃，Python 进程还占着 9999 | `killStaleSimulators()` 每次启动前清理 |
| 手动杀进程后端口仍不通 | `TIME_WAIT` 状态持续 30-120s | 等，或用不同的端口 |

### 7.2 线程崩溃

| 现象 | 原因 | 解决 |
|------|------|------|
| `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN) | 跨线程析构 socket engine | `BlockingQueuedConnection::close` |
| `QObject::killTimer: Timers cannot be stopped from another thread` | ParseWorker 的 QTimer 在 parseThread，析构在 mainThread | 警告无害，但说明析构顺序不对；`close()` 后再 `quit()` |

### 7.3 模拟器脚本路径

| 现象 | 原因 | 解决 |
|------|------|------|
| `QSKIP` 了所有测试 | `QDir::currentPath()` 是 build 目录，不是项目根 | 多候选路径查找 + CMake `PROJECT_SOURCE_DIR` 宏 |

```cpp
QStringList candidates = {
    QDir::currentPath() + "/../tools/tcp_wave_simulator.py",   // from build/
    QString(PROJECT_SOURCE_DIR) + "/tools/tcp_wave_simulator.py", // from CMake
};
for (const auto &c : candidates)
    if (QFile::exists(c)) { m_simScript = c; break; }
```

### 7.4 ctest 输出被吞

| 现象 | 原因 | 解决 |
|------|------|------|
| `ctest` 运行完但无输出 | QtTest 默认不向 stdout 输出（除非指定 `-o -`） | `ctest -V` 或测试二进制加 `-txt -o -` |
| 输出洪水（每帧一行 qInfo） | ProtocolDecoder 100Hz → 每秒 100 行日志 | `set QT_LOGGING_RULES=*.info=false` |

---

## 八、集成测试的可移植性

当前 `killStaleSimulators()` 仅 Windows。跨平台版本：

```cpp
void killStaleSimulators()
{
#ifdef Q_OS_WIN
    QProcess::execute("taskkill", {"/f", "/im", "python.exe"});
#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    QProcess::execute("pkill", {"-f", "tcp_wave_simulator"});
#endif
    QTest::qWait(500);
}
```

---

## 九、核心心法

1. **集成测试验管道，单元测试验逻辑** — 不要用集成测试追求精确值校验
2. **`BlockingQueuedConnection` 是线程安全的开关** — 需要阻塞等跨线程操作完成时用它
3. **子进程启动用端口探测，不要读 stdout** — 进程运行 ≠ 端口就绪
4. **每次测试前清理残留** — `taskkill` + `QTest::qWait` 释放端口
5. **断线重连 = 全新管道 + 同一 DB** — 不要尝试复用 TcpWorker 跨 kill/restart 周期
6. **CI 排除集成测试** — 耗时、依赖外部进程、端口冲突风险
7. **辅助函数封装是集成测试的骨架** — `startSimulator()` / `stopSimulator()` / `stopThreads()` / `killStaleSimulators()` 四个函数支撑了全部测试逻辑
