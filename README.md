# PulseQt — 多通道数据采集上位机

基于 **Qt 6.5+ / C++17** 的轻量级工业数据采集上位机系统。通过串口或 TCP 从下位机/传感器接收实时数据流，完成可视化展示、SQLite 持久化存储和历史回溯。

---

## 功能特性

- **双通道通信** — 串口（RS-232/USB-TTL）与 TCP 客户端，通过统一 `IChannel` 接口抽象
- **自定义二进制协议** — 帧同步头 + 长度 + 类型 + CRC16-CCITT 校验，完整粘包拆包状态机解码
- **实时曲线** — `QPainter` 自绘，支持 8 通道同时绘制，100 Hz 刷新，滚轮缩放 + 拖拽平移
- **SQLite 持久化** — WAL 模式 + 批量事务写入（≥ 500 条/秒），支持按时间范围查询与自动清理
- **多线程架构** — 通信线程 / 解析线程 / UI 线程分离，`moveToThread` + 队列信号槽实现无锁跨线程数据传递
- **历史回放** — 时间轴滑块拖动回放任意时段数据
- **CSV 导出** — 指定时间范围和通道导出为 CSV
- **暗色主题** — QSS 暗色主题，IDE 风格
- **断线重连** — 指数退避策略（1s → 2s → 4s → … → 30s 上限）
- **72 小时稳定运行** — 无崩溃、无内存泄漏

---

## 技术栈

| 类别 | 技术 |
|------|------|
| 语言 | C++17 |
| UI 框架 | Qt 6.5+ (Widgets) |
| 构建系统 | CMake 3.20+ |
| 数据库 | SQLite 3（通过 Qt SQL 模块） |
| 序列化 | 自定义二进制协议（小端序） |
| 测试模拟 | Python 3 + `socat` 虚拟串口 / `ncat` TCP 模拟 |

---

## 架构概览

```
┌─────────────────────────────────────────────────┐
│                   UI 层 (ui/)                     │
│  MainWindow  StatusBar  RealTimeChart            │
│  DataTableView  HistoryPlayer  ExportDialog      │
├─────────────────────────────────────────────────┤
│                数据管理层 (data/)                  │
│  DataTableModel  DataBuffer  DatabaseManager     │
├─────────────────────────────────────────────────┤
│                业务逻辑层 (protocol/)              │
│  ProtocolDecoder  ProtocolValidator              │
├─────────────────────────────────────────────────┤
│                通信层 (communication/)            │
│  IChannel  SerialChannel  TcpChannel             │
│  ChannelManager                                  │
├─────────────────────────────────────────────────┤
│              工作线程层 (worker/)                  │
│  SerialWorker  TcpWorker  ParseWorker            │
└─────────────────────────────────────────────────┘
```

### 线程模型

```
通信线程 (QThread #1) ──rawDataReceived──→ 解析线程 (QThread #2) ──dataPointReady──→ UI 线程 (主线程)
   SerialWorker                                ParseWorker                               MainWindow
   TcpWorker                                   ProtocolDecoder                           DataModel
                                               DataBuffer                                Chart
                                               DatabaseManager
```

所有跨线程通信均使用 `Qt::QueuedConnection`，从设计上杜绝数据竞争。

---

## 通信协议

二进制帧格式（小端序），总帧长 6+N 字节（N ≤ 255）：

```
┌─────────┬─────────┬────────┬──────────────┬──────────┐
│ Header  │ Length  │ Type   │   Payload    │  CRC16   │
│ 2 bytes │ 1 byte  │ 1 byte │   N bytes    │ 2 bytes  │
├─────────┼─────────┼────────┼──────────────┼──────────┤
│ 0xA55A  │  0~255  │ 见下表 │    变长      │ CCITT    │
└─────────┴─────────┴────────┴──────────────┴──────────┘
```

| Type | 名称 | 说明 |
|:----:|------|------|
| `0x01` | 数据帧 | 通道数据负载 |
| `0x02` | 心跳帧 | 保活探测 |
| `0x03` | 心跳应答 | 心跳响应 |
| `0x04` | 握手请求 | 通道配置协商 |
| `0x05` | 握手应答 | 配置确认 |
| `0xFF` | 错误帧 | 错误码 + 描述 |

解码状态机：`WAIT_HEADER → WAIT_LENGTH → WAIT_TYPE → WAIT_PAYLOAD → WAIT_CRC → DONE`

详细协议说明见 [`doc/03_通信协议设计.md`](doc/03_通信协议设计.md)。

---

## 快速开始

### 依赖

- **Qt 6.5+**（Core, Widgets, SerialPort, Network, Sql 模块）
- **CMake 3.20+**
- **C++17 编译器**（GCC 11+ / Clang 14+ / MSVC 2022+）
- **Python 3**（仅模拟测试用）
- **socat**（虚拟串口测试，Linux）

### 构建

```bash
git clone <repo-url> && cd PulseQt
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 运行

```bash
./build/PulseQt
```

### 模拟测试（无需硬件）

```bash
# 1. 创建虚拟串口对
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# 输出类似：/dev/pts/2 和 /dev/pts/3

# 2. 一端运行模拟数据源
python3 tools/simulator.py /dev/pts/2

# 3. 另一端启动上位机，连接 /dev/pts/3
./build/PulseQt
```

TCP 测试：

```bash
ncat -l 9999 -k --exec "python3 tools/simulator.py --tcp"
# PulseQt 中连接 127.0.0.1:9999
```

---

## 项目结构

```
PulseQt/
├── CMakeLists.txt              # 构建配置
├── README.md
├── doc/                        # 设计文档
│   ├── 01_需求规格说明.md
│   ├── 02_架构设计.md
│   ├── 03_通信协议设计.md
│   └── 04_开发计划与日报.md
├── src/                        # 源码
│   ├── main.cpp
│   ├── communication/          # 通信层
│   ├── protocol/               # 协议层
│   ├── data/                   # 数据管理层
│   ├── worker/                 # 工作线程层
│   └── ui/                     # UI 层
├── include/                    # 头文件
├── resources/                  # QSS 主题、图标等资源
└── tools/                      # 辅助工具
    └── simulator.py            # 模拟数据源
```

---

## 性能指标

| 指标 | 目标值 |
|------|--------|
| 数据采集刷新率 | ≥ 100 Hz（10 ms 间隔） |
| 曲线渲染帧率 | ≥ 30 FPS |
| 内存占用（运行态） | < 150 MB |
| SQLite 写入速率 | ≥ 500 条/秒 |
| 启动时间 | < 3 秒 |
| 连续运行稳定性 | 72 小时无崩溃 |

---

## 许可证

[MIT License](LICENSE)
