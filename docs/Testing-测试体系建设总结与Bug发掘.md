# PulseQt 测试体系建设 — 总结与 Bug 发掘实录

> T022–T026 全流程复盘 · 30 用例 · 5 测试二进制 · 15 个 Bug/坑 · 6 项设计原则

---

## 一、测试全景

```
                    PulseQtTestLib (静态库)
                    ├── ProtocolDecoder
                    ├── DataBuffer
                    ├── DatabaseManager
                    ├── TcpWorker  + ParseWorker
                    └── 对应头文件 (AUTOMOC)
                           │
        ┌──────────────────┼──────────────────┬──────────────────┐
        ▼                  ▼                  ▼                  ▼
  TestFrame         TestProtocolDecoder  TestDataBuffer   TestDatabaseManager
  (CRC + 常量)       (状态机 8 用例)      (环形缓冲 8 用例)   (DB 10 用例)
        │                  │                  │                  │
        └──────────────────┴──────────────────┴──────────────────┘
                                    │
                                    ▼
                           TestIntegration
                       (全链路 2 用例 · Python 模拟器)

  .github/workflows/build.yml
  (Ubuntu + Windows CI · ctest 自动验证)
```

| 指标 | 数值 |
|------|:--:|
| 测试二进制 | 5 个 |
| 测试用例 | 30 个 |
| 全量通过率 | 5/5 (100%) |
| 全量耗时 | ~38 秒 |
| CI 平台 | Ubuntu 22.04 + Windows 2022 |

---

## 二、Bug 与踩坑全记录（15 项）

### 🔴 阻塞级（3 项）

#### Bug ① · Frame.h 重复声明 + 重复 `#include`

**位置**：`include/Frame.h`

**现象**：T022 重构时把 `crc16_ccitt()` 提成公共声明，但旧声明没删。`#include <cstddef>` 也出现了两次。

**影响**：编译通过但显得不专业。潜在的链接器歧义风险（若两个声明属性不一致）。

**修复**：删除第一次声明块（含重复 include），仅保留 struct 后、`Q_DECLARE_METATYPE` 后的正式声明。

**教训**：重构后 **grep 函数名全局搜一遍**，确认是否残留旧声明。

---

#### Bug ② · RealTimeChart::valueToPixelY 除零

**位置**：`src/ui/RealTimeChart.cpp:27`

**现象**：`(yMax - yMin)` 作为除数，当所有通道值相同时为 0。

**影响**：当前硬编码 Y 轴 `[0, 1024]` 未触发。v2.0 自适应 Y 轴（T033）时必炸。

**修复**：
```cpp
if (qFuzzyCompare(yMax, yMin))
    return (height() - 40.0) / 2.0;   // 所有值相同 → 居中
```

**教训**：**任何除法都加守卫**——即使当前不触发，下次改 Y 轴逻辑时也不会想起来。

---

#### Bug ③ · 集成测试线程析构崩溃（STATUS_STACK_BUFFER_OVERRUN）

**位置**：`tests/TestIntegration.cpp`

**现象**：`reconnection` 测试中 kill 模拟器后，`QTcpSocket` 的 `QNativeSocketEngine` 在 commThread 内创建，但析构发生在主线程。触发 `QCoreApplication::sendEvent` 断言失败，exit code `0xC0000409`。

**根因**：`QThread::quit() → wait()` 后 TcpWorker 栈对象析构，但 `QTcpSocket` 内部的 socket engine 清理需要在其所属线程完成。

**修复**：
```cpp
// BlockingQueuedConnection: 在 comm 线程内执行 close，主线程阻塞等待
QMetaObject::invokeMethod(&tcp, "close", Qt::BlockingQueuedConnection);
comm.quit(); comm.wait(5000);
```

**教训**：**跨线程对象清理必须在对象所属线程执行**。`BlockingQueuedConnection` 是测试代码里保证这一点的关键武器。

---

### 🟡 功能级（5 项）

#### Bug ④ · 心跳逻辑被误判为有 Bug

**位置**：`src/worker/ParseWorker.cpp:73`

**现象**：代码审查时认为"数据流中每 5s 也发心跳"，但实际逻辑正确。

**分析**：`onRawDataReceived` 每 ~10ms 更新 `m_lastDataTime`（数据流 100Hz）。定时器 5s 触发时，`elapsed = now - m_lastDataTime ≈ 10ms`，`elapsed >= 5000` 为 false，心跳不发送。仅在真正无数据 5s 后才触发。

**结论**：**审查结论需要复现验证**，不要凭静态阅读就下判断——这次是审查者混淆了"距上次定时器触发"和"距上次数据到达"。

---

#### Bug ⑤ · Autogen include 路径 → Qt Creator 误报 `.moc` 找不到

**位置**：`tests/TestFrame.cpp:42`

**现象**：Qt Creator 编辑器红色波浪线 `'TestFrame.moc' file not found`，但 `cmake --build` 编译器能找到。

**根因**：Qt Creator 的 clang-code-model 不感知 AUTOMOC 输出路径 (`build/TestFrame_autogen/include_Debug/`)。

**修复**：
```cmake
target_include_directories(TestFrame PRIVATE
    ${CMAKE_BINARY_DIR}/TestFrame_autogen/include_Debug)
```

**教训**：IDE 报错 ≠ 编译报错。先 `cmake --build` 确认是不是真错误。

---

#### Bug ⑥ · `#include "TestFrame.moc"` 和 AUTOMOC 的冲突

**位置**：`tests/TestFrame.cpp`

**现象**：第一次尝试删除 `.moc` include 行 → AUTOMOC 报错 `contains Q_OBJECT but does not include .moc`。QtTest 的 `.cpp` 中 `Q_OBJECT` 宏必须手动 include moc。

**规则**：`.h` 中 `Q_OBJECT` → AUTOMOC 自动处理；`.cpp` 中 `Q_OBJECT` → 必须手动 `#include "xxx.moc"`。不可删除。

---

#### Bug ⑦ · CMakeLists.txt 测试段重复定义

**位置**：`CMakeLists.txt`

**现象**：多次编辑 `CMakeLists.txt` 时，底部残留了旧的重复测试段（`TestDatabaseManager` 定义了两次），导致 `CMP0002` 策略错误。

**修复**：每次编辑后 `read_file` 确认全文件只有一处定义。

**教训**：多任务追加式编辑 CMake 时容易产生残留——**每次改完读一遍文件末尾**。

---

#### Bug ⑧ · Python 模拟器输出缓冲导致 `waitForReadyRead` 超时

**位置**：`tests/TestIntegration.cpp:startSimulator()`

**现象**：QProcess 启动 Python 后 `waitForReadyRead(10000)` 超时，模拟器明明在运行但读不到输出。

**根因**：Python 默认 stdout 行缓冲，`QProcess` 需要显式读到内容才返回。

**修复**：改用 TCP 端口轮询检测就绪（`QTcpSocket::connectToHost` 循环探测），比读 stdout 可靠 100 倍。

---

### 🟢 工程级（7 项）

#### ⑨ · QtTest 模块默认不安装

**现象**：`find_package(Qt6 COMPONENTS Test)` 报 `'QtTest' file not found`。

**原因**：Qt 在线安装器默认不勾选 Test 模块。需 Maintenance Tool → Add components → 勾选 Qt Test。

---

#### ⑩ · MSVC `/Zc:__cplusplus`

**现象**：MSVC 编译 Qt6 报 `#error: "Qt requires a C++17 compiler"`。

**原因**：MSVC 默认 `__cplusplus` 值不正确。需 `add_compile_options(/Zc:__cplusplus)`。

---

#### ⑪ · Qt DLL → ctest 报 `0xc0000135`

**现象**：手动运行 `test.exe` 正常，`ctest` 报 `STATUS_DLL_NOT_FOUND`。

**原因**：ctest 不继承 IDE 设置的 Qt bin PATH。

**修复**：`set_tests_properties(... ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:...")`（CMake ≥ 3.22）。

---

#### ⑫ · PulseQtTestLib 每次新增模块需同步 3 处

**问题**：新增被测模块时容易漏掉的重复操作：

| 位置 | 操作 |
|------|------|
| `add_library(PulseQtTestLib ...)` | 追加 `.cpp` + `.h` |
| 新增 `qt_add_executable` + `add_test` | 创建测试 target |
| `set_tests_properties` | 追加测试名到 DLL 路径列表 |

T022→T025 每次新增都至少漏一处，CI 或本地 `ctest` 挂掉才想起来。

**建议**：写一个 CMake 宏自动化这 3 步。或至少在注释里提醒。

---

#### ⑬ · `serializeChannels` 是 private → friend class 破局

**位置**：`include/DatabaseManager.h`

**问题**：T024 需要测 `serializeChannels` / `deserializeChannels`，但它们是 private static。

**方案对比**：

| 方案 | 影响 |
|------|------|
| 改成 public | 破坏封装，暴露给所有调用者 |
| 测试里用 `#define private public` | 肮脏，不同编译器行为不一致 |
| **friend class TestDatabaseManager** | ✅ 精确授权，零副作用 |

---

#### ⑭ · 集成测试残留 Python 进程占端口

**现象**：`reconnection` 测试 kill 模拟器后立刻重启，端口 9999 仍被占用（`TIME_WAIT`）。

**修复**：`killStaleSimulators()` — 每次 `startSimulator()` 前先 `taskkill /f /im python.exe`，再 `QTest::qWait(500)` 等端口释放。

**平台注意**：`taskkill` 仅 Windows。Linux/macOS 需 `pkill` 或 `killall`。

---

#### ⑮ · CI 中 Qt 路径硬编码

**位置**：`CMakeLists.txt:23`

**问题**：`set(CMAKE_PREFIX_PATH "D:/Qt/6.9.0/msvc2022_64" ...)` 在 CI 环境不存在此路径，`find_package(Qt6)` 找不到。

**修复**：加 `if(EXISTS "D:/Qt/6.9.0/msvc2022_64")` 守卫，CI 由 `install-qt-action` 提供 Qt 路径。

---

## 三、测试架构总结

### 3.1 三级测试金字塔

```
        ╱ 集成测试 ╲          TestIntegration (2 用例)
       ╱   单元测试   ╲        TestFrame / ProtocolDecoder / DataBuffer / DatabaseManager (28 用例)
      ╱   静态检查    ╲        CMake 配置 + MSVC 编译警告
```

### 3.2 两类被测模块的用例推导法

| 模块类型 | 推导方法 | 示例 |
|------|------|------|
| **状态机类** | 状态 × 刺激 二维展开 | ProtocolDecoder: 7 状态 × {正常, 异常, 边界} |
| **生命周期类** | 生命周期阶段 × 操作 | DataBuffer: {空, 未满, 满, 绕回} × {push, snapshot, clear} |

共通点：**都从被测模块的内在结构出发，不从代码行数出发。**

### 3.3 测试辅助函数模式

| 被测模块 | 辅助函数 | 节省 |
|------|------|------|
| ProtocolDecoder | `buildFrame(type, payload)` | 8 个用例免手拼二进制帧 |
| DataBuffer | `makePoint(ts, ch0, ch1)` | 8 个用例免逐字段构造 |
| TestIntegration | `startSimulator()` / `stopThreads()` / `killStaleSimulators()` | 进程生命周期和线程安全封装 |

**原则**：辅助函数是测试代码里 ROI 最高的投资。

### 3.4 副作用模块的隔离策略

| 模块 | 副作用 | 隔离方式 |
|------|------|------|
| DatabaseManager | SQLite 文件 I/O | `init()` + `cleanup()` 临时文件，用例间物理删除 |
| TcpWorker/ParseWorker | 网络连接 + 心跳定时器 | QThread + `BlockingQueuedConnection` 安全关闭 |
| 全链路 | Python 子进程 | `taskkill` 清理残留 + TCP 端口轮询就绪 |

---

## 四、CMake 测试基础设施演进

```
T022: PulseQtTestLib (ProtocolDecoder) → TestFrame + TestProtocolDecoder
T023: PulseQtTestLib + DataBuffer      → + TestDataBuffer
T024: PulseQtTestLib + DatabaseManager → + TestDatabaseManager (+ Qt6::Sql)
T025: PulseQtTestLib + TcpWorker + ParseWorker → + TestIntegration (+ Qt6::Network)
T026: .github/workflows/build.yml → Ubuntu + Windows CI
```

**当前 PulseQtTestLib 全貌**：
```cmake
add_library(PulseQtTestLib STATIC
    src/protocol/ProtocolDecoder.cpp
    src/data/DataBuffer.cpp
    src/data/DatabaseManager.cpp
    src/worker/TcpWorker.cpp
    src/worker/ParseWorker.cpp
    include/ProtocolDecoder.h
    include/Frame.h
    include/DataBuffer.h
    include/DataPoint.h
    include/DatabaseManager.h
    include/TcpWorker.h
    include/ParseWorker.h
)
target_link_libraries(PulseQtTestLib PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
```

---

## 五、关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 测试框架 | QtTest (`QTEST_MAIN`) | 零额外依赖，与 Qt 项目天然集成 |
| 被测代码复用 | `PulseQtTestLib STATIC` | 单次编译，多测试链接，避免符号冲突 |
| 信号捕获 | `QSignalSpy` | 无需 mock 框架，`Q_DECLARE_METATYPE` 注册自定义类型 |
| private 方法测试 | `friend class` | 精确授权，不破坏封装 |
| 数据库测试 | 临时文件 + `cleanup()` 物理删除 | 用例间零干扰 |
| 集成测试 | QProcess 启动模拟器 + 端口轮询 | 比读 stdout 可靠 |
| 线程清理 | `BlockingQueuedConnection::close` | 确保 socket 在所属线程内关闭 |
| CI Qt 路径 | `if(EXISTS ...)` 守卫 | 本地 CI 双兼容 |
| 集成测试在 CI 中 | `ctest -E Integration` 排除 | 需要 Python 模拟器，不适合 CI runner |

---

## 六、后续改进建议

| 优先级 | 事项 | 说明 |
|:--:|------|------|
| 高 | CI 加 Windows artifact 自动打包 NSIS | T026 已预留 `upload-artifact` |
| 高 | CI 加 `gcov`/`gcovr` 生成覆盖率报告 | T022 已有 gcovr 命令模板 |
| 中 | CMake 宏自动化"新增测试 target"3 步 | 避免漏加 DLL 路径/autogen 目录 |
| 中 | `killStaleSimulators()` 跨平台化 | 当前仅 `taskkill`，Linux 需 `pkill` |
| 低 | 集成测试加入 CI（用 `com0com` 或条件跳过） | 需 CI runner 装 Python + 依赖 |
| 低 | 线程清理警告静默化 | `QObject::killTimer` 警告无害但吵 |
