# QtTest 单元测试 — 设计方法与实施全解

> 基于 T022 ProtocolDecoder + T023 DataBuffer 测试实战，覆盖测试架构设计、两类模块用例推导（状态机/非状态机）、CMake 增量扩展、踩坑与最佳实践。

---

## 一、测试架构：分层与依赖隔离

### 1.1 核心原则：被测单元零 Mock、零副作用

两个模块都是理想的单元测试对象——纯内存操作、无外部依赖：

| 特征 | ProtocolDecoder | DataBuffer |
|------|:--|:--|
| 输入 | `feed(QByteArray)` 字节流 | `push(DataPoint)` 数据点 |
| 输出 | signal `frameDecoded` / `crcError` | signal `bufferUpdated` + `snapshot()` |
| 依赖 | 无外部服务/文件 IO/网络 | QMutex（标准库，无外部依赖） |
| 状态 | 7 状态状态机 | 环形缓冲 + 头指针 |
| 可重置 | `reset()` | `clear()` |
| 适合单测 | ✅ | ✅ |

| 特征 | ❌ 不适合单测的典型 |
|------|------|
| 依赖 | 需要 SQLite 文件、网络连接、硬件串口 |
| 副作用 | 写入磁盘、发送网络包、修改系统状态 |
| 处理 | T024 用临时文件隔离，T025 用 Python 模拟器打桩 |

**选择原则**：优先测"输入→输出"清晰的模块（协议解码、数据缓冲、序列化），后测有副作用的模块（数据库写入、网络通信）。

### 1.2 CMake 测试基础设施

```
PulseQtTestLib (STATIC)               ← 公共：复用被测 .cpp
  ├── ProtocolDecoder.cpp             ← T022 被测模块
  ├── DataBuffer.cpp                  ← T023 被测模块
  ├── ProtocolDecoder.h, Frame.h      ← AUTOMOC 扫描生成 moc
  └── DataBuffer.h, DataPoint.h       ← AUTOMOC 扫描生成 moc
       │
       ├── TestFrame.exe              ← CRC 标准向量
       ├── TestProtocolDecoder.exe    ← 状态机 8 用例
       └── TestDataBuffer.exe         ← 环形缓冲 8 用例含多线程
```

**为什么用静态库？** 被测模块的 .cpp 只编译一次，所有测试可执行文件链接同一个 PulseQtTestLib，避免重复编译和符号冲突。

**关键 CMake 片段：**

```cmake
# 测试公共静态库（每个新任务追加被测 .cpp / .h）
add_library(PulseQtTestLib STATIC
    src/protocol/ProtocolDecoder.cpp
    src/data/DataBuffer.cpp              # ← T023 追加
    include/ProtocolDecoder.h
    include/Frame.h
    include/DataBuffer.h                 # ← T023 追加
    include/DataPoint.h                  # ← T023 追加
)
target_link_libraries(PulseQtTestLib PUBLIC Qt6::Core)

# 测试可执行文件（每个新任务追加一个 target）
qt_add_executable(TestProtocolDecoder tests/TestProtocolDecoder.cpp)
target_link_libraries(TestProtocolDecoder PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME ProtocolDecoder COMMAND TestProtocolDecoder)

qt_add_executable(TestDataBuffer tests/TestDataBuffer.cpp)
target_link_libraries(TestDataBuffer PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME DataBuffer COMMAND TestDataBuffer)
```

### 1.3 MSVC 特有配置

```cmake
# MSVC 需显式开启 __cplusplus（否则 Qt6 报错：requires C++17 compiler）
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# 多版本 Qt 共存时强制指定路径（避免误用 mingw_64）
set(CMAKE_PREFIX_PATH "D:/Qt/6.9.0/msvc2022_64" ${CMAKE_PREFIX_PATH})
```

### 1.4 测试运行时 DLL 路径

```cmake
# ctest 启动时 Qt DLL 不在 PATH 中 → ENVIRONMENT_MODIFICATION (CMake ≥ 3.22)
set_tests_properties(Frame ProtocolDecoder DataBuffer PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:D:/Qt/6.9.0/msvc2022_64/bin")
```

---

## 二、用例设计逻辑：从状态机推导测试

### 2.1 设计方法：状态空间覆盖

ProtocolDecoder 的 7 状态状态机是天然的用例生成器。对每个状态列出期望行为和异常路径：

```
状态机 7 状态 × 3 类刺激（正常/异常/边界）= 8 用例

WAIT_HEADER_H  → ⑥ 乱码过滤（非 0xA5 跳过）
WAIT_HEADER_L  → ⑤ Payload 内的假帧头不误判
WAIT_LENGTH    → ⑧ Length=0 跳过 WAIT_PAYLOAD
WAIT_TYPE      → ① 正常帧一路到底
WAIT_PAYLOAD   → ③ 半帧续传、② 粘包
WAIT_CRC_L     → ④ CRC 篡改 → crcError
WAIT_CRC_H     → ① 校验通过 → frameDecoded
reset()        → ⑦ 中途重置
```

### 2.2 8 个用例清单 + 设计动机

| # | 用例 | 覆盖场景 | 为什么重要 |
|---|------|------|-----------|
| ① | singleFrame | 完整帧解码，全 7 状态 | 快乐路径——基础功能 |
| ② | stickyPacket | 两帧粘在一起，一次 feed | TCP 粘包是真实场景 |
| ③ | halfFrame | 帧从中切开，两次 feed | TCP 拆包是真实场景 |
| ④ | crcError | 篡改 CRC → crcError + 状态恢复 | 数据损坏不崩溃 |
| ⑤ | headerInPayload | Payload 含 0xA55A 不误判 | 帧同步最易出 Bug 的边界 |
| ⑥ | randomNoise | 1000 字节随机 → 不崩溃 + 恢复 | 鲁棒性底线 |
| ⑦ | resetMidStream | 半帧 → reset → 新帧 | 通道切换场景 |
| ⑧ | emptyPayload | 心跳帧 Payload=0 | 空帧曾有 Bug（残留上一帧数据） |

### 2.3 信号捕获模式

QtTest 用 `QSignalSpy` 捕获信号，无需回调：

```cpp
ProtocolDecoder decoder;
QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);
QSignalSpy errSpy(&decoder, &ProtocolDecoder::crcError);

decoder.feed(buildFrame(0x01, payload));

QCOMPARE(spy.count(), 1);                          // 信号触发次数
QCOMPARE(errSpy.count(), 0);                       // 错误信号未触发

Frame frame = spy.at(0).at(0).value<Frame>();      // 取第一个信号的第一个参数
QCOMPARE(frame.type, static_cast<uint8_t>(0x01));
QCOMPARE(frame.payload, payload);
```

**关键前置条件**：自定义类型必须注册元类型。

```cpp
// Frame.h
#include <QMetaType>
Q_DECLARE_METATYPE(Frame)   // ← 全局作用域，struct 花括号外
```

### 2.4 测试辅助函数：buildFrame

不要在每个用例里手拼二进制帧——写一个可复用的构建函数：

```cpp
static QByteArray buildFrame(uint8_t type, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA5));               // Header H
    frame.append(static_cast<char>(0x5A));               // Header L
    frame.append(static_cast<char>(payload.size()));     // Length
    frame.append(static_cast<char>(type));               // Type
    frame.append(payload);                               // Payload

    uint16_t crc = crc16_ccitt(
        reinterpret_cast<const uint8_t *>(frame.constData()),
        frame.size());

    frame.append(static_cast<char>(crc & 0xFF));         // CRC 低字节（小端序）
    frame.append(static_cast<char>((crc >> 8) & 0xFF));  // CRC 高字节
    return frame;
}
```

**设计要点**：
- 单帧内字段顺序 = 协议帧格式，白盒对照一目了然
- CRC 计算基于 `frame` 当前内容（Header+Length+Type+Payload），与 ProtocolDecoder 的校验计算范围一致
- 小端序 CRC 低字节先追加，与协议设计文档一致

---

## 三、框架选型：QTEST_MAIN vs 手动 main

### 3.1 标准写法

```cpp
#include <QtTest>

class TestFoo : public QObject
{
    Q_OBJECT

private slots:          // ← private slots，不是 private
    void testCase1()
    {
        QCOMPARE(2 + 2, 4);
    }
};

QTEST_MAIN(TestFoo)         // 自动生成 main() + 事件循环
#include "TestFoo.moc"      // QtTest 惯例：显式 include moc
```

### 3.2 QCOMPARE vs QVERIFY

| 宏 | 用途 | 失败信息 |
|----|------|---------|
| `QCOMPARE(a, b)` | 相等断言 | 输出 a 和 b 的实际值 |
| `QVERIFY(cond)` | 布尔断言 | 只输出 "FALSE" |
| `QVERIFY2(cond, msg)` | 带消息的布尔断言 | 输出自定义消息 |

**原则**：能用 `QCOMPARE` 就不用 `QVERIFY`，失败的诊断信息差十倍。

### 3.3 浮点比较

```cpp
// 不推荐
QCOMPARE(result, 0.29B1);   // 浮点误差导致假失败

// 推荐（仅当涉及时使用）
QVERIFY(qAbs(result - expected) < 1e-9);
```

---

## 四、文件组织

```
tests/
├── TestFrame.cpp              ← 小粒度：只测 Frame 结构 + CRC
└── TestProtocolDecoder.cpp    ← 大粒度：状态机 8 用例
```

**拆分原则**：一个测试文件对应一个被测头文件/模块。不要把所有测试塞进一个文件。

**命名规范**：`Test<模块名>.cpp`，类名 `Test<模块名>`，用例函数名 `camelCase` 描述场景。

---

## 五、实施流程（以 T022 为模板）

### Step 1：前置重构

把被测模块的依赖提到可链接的位置：

- `crc16_ccitt()` 从 ProtocolDecoder.cpp 文件级函数 → Frame.h 声明 + ProtocolDecoder.cpp 保留实现
- `Frame` 结构体加 `Q_DECLARE_METATYPE`（QSignalSpy 捕获需要）
- 确认被测模块不依赖 Logger / QMessageBox 等重依赖（如果不依赖 → 测试库不链接 Widgets）

### Step 2：CMake 基础设施

1. `find_package(Qt6 REQUIRED COMPONENTS ... Test)`
2. `enable_testing()`
3. 创建 `PulseQtTestLib STATIC`（只含被测 .cpp + 对应 .h）
4. `qt_add_executable` + `target_link_libraries(... Qt6::Test)`
5. `add_test(NAME ... COMMAND ...)`
6. 处理 DLL 路径（`ENVIRONMENT_MODIFICATION`）

### Step 3：先写最简单的用例跑通

永远从"快乐路径"开始——`TestFrame::crc16_standardVector` 只依赖一个函数，最适合验证 CMake + 编译 + 链接 + DLL 全链路畅通。

### Step 4：逐个加用例

每写完一个用例 → `cmake --build` → `ctest` 立即验证。不要等全部写完再跑。

### Step 5：覆盖率检查（可选）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build
ctest --test-dir build
gcovr -r . --html-details -o coverage.html
```

---

## 六、常见踩坑

### 6.1 QSignalSpy 捕获不到信号

| 现象 | 原因 | 解决 |
|------|------|------|
| `spy.count() == 0` 但信号确实触发了 | 自定义类型未注册元类型 | 加 `Q_DECLARE_METATYPE(Frame)` |
| 编译通过但 spy 仍为空 | 类型定义在 `#ifdef` 内 / 命名空间不一致 | 确认 `Q_DECLARE_METATYPE` 在全局作用域 |
| `value<Frame>()` 返回默认值 | 拷贝构造/赋值问题 | Frame 是纯数据 struct，默认成员级拷贝即可 |

### 6.2 AUTOMOC + 静态库导致 MOC 缺失

| 现象 | 原因 | 解决 |
|------|------|------|
| `LNK2019: frameDecoded` 无法解析 | 头文件不在静态库源列表中，AUTOMOC 未扫描 | 把 `.h` 文件也加入 `add_library` 源列表 |
| 重复定义 | 头文件同时出现在两个 target 的源列表 | 这是正常的——每个 target 独立生成 moc |

### 6.3 QtTest 找不到

| 现象 | 原因 | 解决 |
|------|------|------|
| `'QtTest' file not found` | Qt 在线安装器默认不勾选 Test 模块 | Qt Maintenance Tool → 勾选 Qt Test → 安装 |
| `find_package(Qt6 COMPONENTS Test)` 失败 | Qt6_DIR 指向了不含 Test 的版本 | 检查 `CMAKE_PREFIX_PATH` 是否正确指向完整安装 |

### 6.4 MSVC `/Zc:__cplusplus`

| 现象 | 原因 | 解决 |
|------|------|------|
| `#error: "Qt requires a C++17 compiler"` | MSVC 默认不定义正确的 `__cplusplus` 值 | `add_compile_options(/Zc:__cplusplus)` |

### 6.5 Qt DLL 找不到

| 现象 | 原因 | 解决 |
|------|------|------|
| 手动运行 `.exe` 正常，`ctest` 报 `0xc0000135` | ctest 启动时不继承 IDE 设置的环境变量 | `ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:..."` |

### 6.6 测试用例跑完不退出

| 现象 | 原因 | 解决 |
|------|------|------|
| 程序挂起不返回 | QTimer/事件循环未停止 | `QTEST_MAIN` 自动处理；若模块内有异步对象 → `QTimer::singleShot(0, qApp, &QCoreApplication::quit)` |

---

## 七、多线程模块测试（T023 实战）

### 7.1 多线程测试的设计难点

DataBuffer 与 ProtocolDecoder 的本质区别：

| 维度 | ProtocolDecoder | DataBuffer |
|------|:--|:--|
| 线程模型 | 单线程（feed 在同一个线程调用） | 多线程（push 可并发，QMutex 保护） |
| 用例推导 | 从状态机 7 状态 + 3 类刺激 | 从环形缓冲 4 阶段：空/未满/满/绕回 |
| 信号验证 | QSignalSpy 精确计数 | 仅 bufferUpdated 简单信号 |
| 并发验证 | 不需要 | **多线程压测是核心** |
| 数据正确性 | 逐字节对比 | 时间戳顺序对比 |

### 7.2 环形缓冲用例推导：四阶段法

```
缓冲区生命周期 × 操作类型 = 8 用例

阶段 ① 空缓冲      → snapshotEmpty     (边界)
阶段 ② 未满        → pushUnderCapacity  (普通) → snapshotNotFull (验证顺序)
阶段 ③ 满（临界）   → pushOverCapacity   (边界) → snapshotWrapped (验证绕回)
阶段 ④ 清空后重用   → clearBuffer       (生命周期)

通用                 → signalBufferUpdated  (信号)
                     → multithreadedStress  (并发安全)
```

**与状态机推导的区别**：状态机按"状态 × 刺激"二维展开；环形缓冲按"生命周期阶段 × 操作"展开。共通点——都是从被测模块的内在结构出发，不是从代码行数出发。

### 7.3 多线程压测模式

```cpp
void multithreadedStress()
{
    const int CAPACITY   = 100;
    const int PER_THREAD = 5000;
    DataBuffer buffer(CAPACITY);

    // ── Writer 1 ──
    QThread t1;
    QObject::connect(&t1, &QThread::started, [&]() {
        for (int i = 0; i < PER_THREAD; ++i)
            buffer.push(makePoint(i));
        t1.quit();
    });

    // ── Writer 2 ──
    QThread t2;
    QObject::connect(&t2, &QThread::started, [&]() {
        for (int i = 0; i < PER_THREAD; ++i)
            buffer.push(makePoint(1'000'000 + i));
        t2.quit();
    });

    t1.start();
    t2.start();

    // ── Reader（主线程）：持续并发 snapshot ──
    while (t1.isRunning() || t2.isRunning()) {
        QVector<DataPoint> snap = buffer.snapshot();
        QVERIFY(snap.size() <= CAPACITY);  // 核心断言：永不超过容量
    }

    t1.wait();
    t2.wait();
    QCOMPARE(buffer.size(), CAPACITY);
}
```

**关键设计**：
- 写线程用 `QThread::started` + lambda，写完主动 `quit()`
- 读线程（主线程）在 while 循环中持续 snapshot——**snapshot 返回副本**是安全的关键，QMutex 仅在做副本的瞬间持有
- 断言聚焦"不变量"：size 永远 ≤ capacity
- 不用 TSan 也能在 CI 中捕获大部分 data race——如果 QMutex 有遗漏，几十万次并发 push 大概率触发 size 错乱或 crash

### 7.4 DataBuffer 特有的辅助函数模式

```cpp
// 工厂函数：避免每个用例手写 DataPoint 构造
static DataPoint makePoint(uint64_t ts, double ch0 = 0.0, double ch1 = 0.0)
{
    DataPoint dp;
    dp.timestamp = ts;
    dp.channels = {ch0, ch1};
    return dp;
}

// 使用：一行创建
buffer.push(makePoint(42, 3.14, 2.71));
```

**原则**：辅助函数永远值得写——ProtocolDecoder 的 `buildFrame()` 节省了帧拼接，DataBuffer 的 `makePoint()` 节省了 DataPoint 逐字段赋值。

### 7.5 新增踩坑：静态库增量扩展

| 现象 | 原因 | 解决 |
|------|------|------|
| 新测试编译通过但链接报 `LNK2019` | 被测 .cpp 未加入 PulseQtTestLib | 在 CMake 的 `add_library(PulseQtTestLib ...)` 中追加 .cpp + .h |
| AUTOMOC 不生成新模块的 moc | 头文件未在源列表中出现 | `.h` 也必须加入 PulseQtTestLib 源列表 |
| `set_tests_properties` 对新测试不生效 | DLL 路径配置没更新 | 每次新增测试名要追加到 `set_tests_properties` |

---

## 八、后续 T024–T025 注意事项

| T 任务 | 被测模块 | 新增挑战 |
|--------|------|------|
| T024 DatabaseManager | SQLite 读写 | 需要临时数据库文件、测试后清理 |
| T025 集成测试 | 全链路 | 需启动 Python 模拟器、等待端口就绪、超时控制 |

**T024 数据库隔离**：

```cpp
void initTestCase()
{
    m_dbPath = QDir::tempPath() + "/test_pulseqt.db";
    m_mgr.init(m_dbPath);
}

void cleanupTestCase()
{
    QFile::remove(m_dbPath);
}
```

---

## 九、核心心法

1. **先跑通框架，再加用例** — 一个 `QCOMPARE(1, 1)` 验证编译/链接/DLL 全链路
2. **用例从模块内在结构推导** — 状态机按"状态 × 刺激"展开，环形缓冲按"生命周期阶段 × 操作"展开，共通点是都从被测模块的内部结构出发
3. **辅助函数是 ROI 最高的投资** — `buildFrame()` 节省 8 个用例的帧拼接；`makePoint()` 节省 DataPoint 逐字段赋值
4. **每个用例只测一件事** — `crcError` 只测 CRC 失败，不顺便测粘包；`pushOverCapacity` 只测绕回，不顺便测 snapshot 顺序
5. **失败信息要能定位** — `QCOMPARE` 优于 `QVERIFY`，变量名自文档化
6. **多线程测试聚焦不变量** — 不追求逐条数据的精确时序，只断言 `size ≤ capacity` 这类必然成立的条件
7. **CMake 增量扩展是重复劳动的高发区** — 新模块必须同步追加 3 处：PulseQtTestLib 源列表、测试 target、DLL 路径属性
