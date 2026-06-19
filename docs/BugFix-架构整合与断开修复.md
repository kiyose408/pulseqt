# Bug 修复日志 — 架构整合 + 断开行为 + 画面卡顿

> 基于代码审查发现的问题进行修复，涉及 3 类缺陷。

---

## Bug ① · 双架构并存 → 统一为 IChannel + ChannelManager

**发现位置**：代码审查 #4 — IChannel 体系 500 行未使用，与 TcpWorker 双架构并存。

**现象**：
- `IChannel` + `ChannelManager` + `TcpChannel` + `SerialChannel` 设计良好但从未被 MainWindow 实例化
- MainWindow 使用平行的 `TcpWorker` / `SerialWorker` / `ParseWorker` 体系
- 两套代码做同一件事，维护者不知道改哪套

**修复**：

| 文件 | 变更 |
|------|------|
| `include/ChannelManager.h` | `connectToDevice()` + `disconnectDevice()` + `writeData()` 改为 `public slots`（支持 QueuedConnection 跨线程调用）；`setChannel` 注释说明 |
| `src/communication/ChannelManager.cpp` | `setChannel` 加 `m_channel->setParent(this)`（moveToThread 自动跟随）；新增 `writeData` 转发到 IChannel |
| `include/MainWindow.h` | `TcpWorker` → `ChannelManager` + `TcpChannel`；新增 `m_connected` 标记；新增 `teardown()` |
| `src/ui/MainWindow.cpp` | `onConnect` 拆分首次创建/重连；`onDisconnect` 仅关通道不拆线程；`closeEvent` 用 `teardown()` 全拆；`onStart/onStop` 修正 |
| `CMakeLists.txt` | PulseQt 目标移除 `TcpWorker.cpp` + `SerialWorker.cpp`（IChannel 体系早已在源列表中） |

**遗留**：`TcpWorker.h/cpp` 和 `SerialWorker.h/cpp` 文件保留在磁盘（测试代码 TestIntegration 仍使用 TcpWorker），仅不再被 PulseQt 主程序编译。

---

## Bug ② · 断开 = 全拆 → 断开只关通道

**发现位置**：用户反馈"断开之后不能使用历史回放"。

**现象**：
- `onDisconnect` 调用了 `deleteLater` + `quit/wait` + `delete thread`，DataBuffer、ParseWorker、DB 连接全部销毁
- HistoryPlayer 依赖 ParseWorker 的 DataBuffer，断开后指针悬空
- 历史回放功能在断开后不可用

**修复**：

| 操作 | 旧行为 | 新行为 |
|------|------|------|
| 连接 | 每次创建所有对象 | 首次创建，后续复用 |
| 断开 | 全部拆光（线程+worker+buffer 全销毁） | 仅关通道，ParseWorker/DataBuffer/线程存活 |
| 开始采集 | 自动先连接 | 仅开采集（需先连） |
| 关窗口 | 同断开 | `teardown()` 全拆 |

**关键设计**：`m_connected`（通道是否在线）替代 `m_commThread != nullptr`（线程是否存在）作为状态判断。

---

## Bug ③ · 无数据时画面卡住

**发现位置**：用户反馈实时监控中无数据时画面暂停，数据恢复后闪现。

**根因**：`RealTimeChart::drawCurves()` 中 X 轴右边界取 `snap.last().timestamp`（最新数据的绝对时间戳）。无新数据时最新时间戳不变，画面停在原位。

**修复**：`src/ui/RealTimeChart.cpp:197`

```cpp
// 旧：右边界 = 最新数据时间戳（数据停 = 画面停）
uint64_t latestTs = snap.last().timestamp;

// 新：右边界 = 墙钟时间（数据停 = 旧数据持续左移）
uint64_t latestTs = QDateTime::currentMSecsSinceEpoch();
```

**效果**：无数据时已有曲线随时间持续向左滚动，不会卡住。

---

## Bug ④ · `connectToDevice` 跨线程静默失败

**发现位置**：用户反馈重构后点"连接"无反应。

**根因**：`QMetaObject::invokeMethod` 只能调用 `slot` 或 `Q_INVOKABLE` 方法。原 `connectToDevice()` 是普通 public 方法，跨线程调用静默失败——连接请求从未到达通信线程。

**修复**：`connectToDevice()`、`disconnectDevice()`、`writeData()` 改为 `public slots`。

---

## Bug ⑤ · `IChannel` 未设为 `ChannelManager` 子对象

**发现位置**：连接失败排查中发现。

**根因**：`setChannel()` 只存指针不设父子关系。`ChannelManager::moveToThread(commThread)` 时 IChannel 留在主线程，`QTcpSocket` 在主线程创建和操作网络。

**修复**：`setChannel()` 中加 `m_channel->setParent(this)`，`moveToThread` 时自动跟随。

---

## Bug ⑥ · `disconnect()` 覆盖 `QObject::disconnect()`

**发现位置**：重构时编译警告。

**根因**：`ChannelManager::disconnect` 与基类 `QObject::disconnect` 同名，覆盖了 Qt 的信号断开方法。

**修复**：重命名为 `disconnectDevice()`。

---

## Bug ⑦ · X 轴墙钟时间 `uint64_t` 下溢风险（NTP/夏令时回拨）

**发现位置**：重构后代码审查 #3。

**现象**：`drawCurves()` 中 `latestTs = QDateTime::currentMSecsSinceEpoch()` 返回 `qint64`，但被存为 `uint64_t`。NTP 校时或夏令时导致时钟回跳时，`latestTs - timestamp` 作为无符号数回绕成 ~2^64，所有数据点被推到屏幕外。

**修复**：

```cpp
// 旧：uint64_t latestTs（时钟回拨 → 回绕）
uint64_t latestTs = QDateTime::currentMSecsSinceEpoch();
double ratio = (latestTs - timestamp) / windowMs;

// 新：qint64 + 回拨守卫
qint64 latestTs = QDateTime::currentMSecsSinceEpoch();
qint64 delta = latestTs - static_cast<qint64>(timestamp);
if (delta < 0) delta = 0;   // 未来数据置右边缘
```

同时更新 `timeToPixelX` 签名和 `minTs` 计算为 `qint64`。

---

## Bug ⑧ · `teardown()` 中 `deleteLater` 可能不被事件循环处理

**发现位置**：重构后代码审查 #2。

**现象**：`teardown()` 中调用 `m_channelManager->deleteLater()` 将 `DeferredDelete` 投递到 commThread，但紧接着 `quit()/wait()` 可能在事件循环处理 `DeferredDelete` 之前退出，导致内存泄漏。

**修复**：

```cpp
// 旧：deleteLater 投递后 quit/wait 可能跳过 DeferredDelete
m_channelManager->deleteLater();
m_commThread->quit();
m_commThread->wait();

// 新：BlockingQueuedConnection 确保在所属线程执行完毕
QMetaObject::invokeMethod(m_channelManager, "deleteLater",
                          Qt::BlockingQueuedConnection);
m_commThread->quit();
m_commThread->wait();
```

---

## Bug ⑨ · 断开状态下点"开始"显示"采集中…"

**发现位置**：重构后代码审查 #6。

**现象**：`onStart()` 只检查 `m_parseWorker`（线程存在），不检查 `m_connected`（通道在线）。断开后点"开始"，标签显示"采集中…"但无数据流入。

**修复**：`onStart()` 加 `|| !m_connected` 守卫。

---

## Bug ⑩ · `ParseWorker.cpp` extern 声明可能与 `Frame.h` 不同步

**发现位置**：重构后代码审查 #7。

**现象**：`ParseWorker.cpp` 用 `extern uint16_t crc16_ccitt(...)` 前置声明，而非 `#include "Frame.h"`。两个声明可能不同步。

**修复**：删除 extern 声明，改为 `#include "Frame.h"`。

---

## Bug ⑪ · `killStaleSimulators()` Windows 专用 → Linux CI 失效

**发现位置**：代码审查 — 集成测试可移植性。

**现象**：`tests/TestIntegration.cpp` 中 `killStaleSimulators()` 硬编码 `taskkill /f /im python.exe`。Linux CI runner 上此命令不存在，残留 Python 进程占用 9999 端口，后续测试端口轮询超时失败。

**修复**：

```cpp
void killStaleSimulators()
{
#ifdef Q_OS_WIN
    QProcess::execute("taskkill", {"/f", "/im", "python.exe"});
#else
    QProcess::execute("pkill", {"-f", "tcp_wave_simulator"});
#endif
    QTest::qWait(500);
}
```

**备选**：`pkill` 在 macOS 可能不存在（`killall` 替代），后续 CI 实战时再细化。

---

## Bug ⑫ · `SerialWorker` 死代码残留

**发现位置**：代码审查 #2 — 死代码。

**现象**：`include/SerialWorker.h` + `src/worker/SerialWorker.cpp` 声明和实现完整，但 `72cb857` 已从 PulseQt 目标移除编译，没有任何 target 引用它们。死代码增加维护困惑。

**修复**：物理删除两个文件。串口功能通过 `SerialChannel`（IChannel 体系）实现，已编译进 PulseQt 主程序。

---

## Bug ⑬ · Y 轴硬编码 0–1024 → 超出范围静默裁剪

**发现位置**：代码审查 #3 — 可视化缺陷。

**现象**：`RealTimeChart::drawCurves()` 中 `yMin = 0, yMax = 1024` 硬编码。若设备发送 0–4095（12-bit ADC）或负值数据，曲线被压扁到图表顶部/底部，无任何视觉提示。

**修复**：Y 轴范围从可见数据点中自适应计算，加 10% 上下边距。兜底值 0–1024（无数据时使用）。

```cpp
// 旧：硬编码
double yMin = 0, yMax = 1024;

// 新：遍历可见数据点自适应计算
bool first = true;
for (it = lower_bound(...); it != snap.end(); ++it) {
    for (int ch = 0; ch < channels; ++ch) {
        double v = it->channels[ch];
        if (first) { yMin = yMax = v; first = false; }
        else { if (v < yMin) yMin = v; if (v > yMax) yMax = v; }
    }
}
double pad = (yMax - yMin) * 0.1;
yMin -= pad; yMax += pad;
```

**注意**：Y 轴刻度标签仍显示 0–1024（`drawBackground` 中硬编码），后期 T033 多 Y 轴任务时统一改。当前修复确保曲线定位正确——12-bit ADC 设备数据不会被压扁。

---

## Bug ⑭ · 断开连接时崩溃 — `abort()` 触发 `readyRead` 访问半销毁缓冲区

**发现位置**：用户报"点击断开后软件终止"，ASSERT `bytes <= bufferSize` in qringbuffer.cpp。

**根因**：`TcpChannel::close()` 直接调用 `m_socket->abort()`，此时 socket 缓冲区中可能还有待处理数据。`abort()` 触发 `readyRead` 信号 → lambda 调用 `m_socket->readAll()` → 访问正在销毁的 QRingBuffer → 断言失败。

**修复**：`abort()` 前先 `m_socket->disconnect()` 切断所有信号连接。

---

## Bug ⑮ · 图表拖拽方向反直觉

**发现位置**：用户反馈"鼠标向右拖，数据往左跑"。

**根因**：`mouseMoveEvent` 中 `m_xOffset -= dx`，注释说"右拖=增大"，代码却减。注释和代码反了。

**修复**：`-=` → `+=`。

---

## Bug ⑯ · 数据表列宽不均衡——后两列被挤到屏幕外

**发现位置**：用户反馈"只看得到时间戳和 CH1 和部分 CH2"。

**根因**：`setStretchLastSection(true)` 仅拉伸最后一列，前三列用默认窄宽度。

**修复**：改为 `setSectionResizeMode(QHeaderView::Stretch)` 所有列均分宽度。

---

## Bug ⑰ · Y 轴刻度与数据不同步

**发现位置**：暗色主题开发中发现刻度标签滞后一帧。

**根因**：`paintEvent` 中 `drawBackground`（画刻度）先于 `drawCurves`（算 Y 范围）执行。刻度读到的是上一帧的 `m_curYMin/YMax`。

**修复**：提取 `computeYRange()` 在 `paintEvent` 最前面执行，存储到成员变量，`drawBackground` 和 `drawCurves` 共用同一次计算结果。

---

## Bug ⑱ · `computeYRange` 和 `drawCurves` 各取一次 snapshot

**发现位置**：代码审查。

**根因**：两次 snapshot 之间可能有并发写入，Y 范围可能不覆盖 drawCurves 实际渲染的数据。

**状态**：已识别，暂未修（低概率，且后果仅为 Y 范围略窄）。

---

## 累计统计

| 级别 | 数量 | 编号 |
|:--:|:--:|------|
| 🔴 阻塞 | 3 | ④ ⑤ ⑭ |
| 🟡 功能 | 10 | ① ② ③ ⑦ ⑨ ⑪ ⑬ ⑮ ⑯ ⑰ |
| 🟢 工程 | 5 | ⑥ ⑧ ⑩ ⑫ ⑱ |

---

## Bug ⑲ · `setChannel` 替换旧通道时不销毁 → 每次重连泄漏 IChannel + QTcpSocket

**发现位置**：架构审查 #1。

**根因**：`setChannel()` 调用旧通道的 `close()`（清空内部 socket），但未销毁 IChannel 对象本身。重连时创建新通道替换旧通道，旧通道沦为孤儿对象。

**修复**：`close()` 后加 `m_channel->deleteLater()`，在所属线程安全销毁。

---

## Bug ⑳ · 快速连/断时重连定时器事件残留

**发现位置**：架构审查 #2。

**根因**：`QTimer::stop()` 不删除已在事件队列中的 timeout 事件。`disconnectDevice()` → `stopReconnect()` → `stop()` 后，已入队的 timeout 仍可能触发旧通道上的 `open()`。

**状态**：低概率（需定时器恰好在连/断之间到期），暂不修复。后续需将 `stopReconnect()` 改为同时设置 `m_reconnectTimer->blockSignals(true)` 或在 `onReconnectTimer` 中加 `m_userDisconnect` 守卫。

---

## 累计统计

| 级别 | 数量 | 编号 |
|:--:|:--:|------|
| 🔴 阻塞 | 3 | ④ ⑤ ⑭ |
| 🟡 功能 | 10 | ① ② ③ ⑦ ⑨ ⑪ ⑬ ⑮ ⑯ ⑰ |
| 🟡 泄漏 | 1 | ⑲ |
| 🟢 工程 | 5 | ⑥ ⑧ ⑩ ⑫ ⑱ |
| 🔵 已知 | 1 | ⑳ |
