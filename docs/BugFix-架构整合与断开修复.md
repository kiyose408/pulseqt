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
