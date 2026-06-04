# PulseQt — 多通道数据采集上位机

> 基于 **Qt 6.5 / C++17** 的轻量级工业数据采集系统。通过 TCP 或串口接收传感器二进制数据流，提供实时曲线渲染、SQLite 持久化存储与历史回放功能。

---

## 1. 功能特性

### 通信与协议

| 模块 | 实现方式 |
|------|------|
| **双通道支持** | TCP 客户端（QTcpSocket）/ 串口（QSerialPort），可扩展为任意 IChannel 实现 |
| **自定义二进制协议** | 2 字节帧同步头 `0xA55A` + 1 字节长度 + 1 字节类型 + N 字节负载 + 2 字节 CRC16-CCITT（小端序） |
| **粘包拆包** | 7 状态有限状态机（`WAIT_HEADER_H → ... → WAIT_CRC_H`），逐字节推进，天然支持 TCP 粘包/半帧 |
| **完整性校验** | CRC16-CCITT 查表法（多项式 `0x1021`），校验失败丢弃并记录 WARN 日志 |
| **断线重连** | 指数退避策略：1s → 2s → 4s → … → 30s 封顶，重连成功后计数器归零 |
| **心跳保活** | 5s 空闲发送心跳帧（type=0x02），连续 6 次无应答（30s）判定断线 |

### 数据管理

| 模块 | 实现方式 |
|------|------|
| **环形缓冲区** | 预分配 `QVector` + 头指针绕回，容量 10000 条，`QMutex` 保证跨线程安全 |
| **SQLite 持久化** | WAL 模式 + 批量事务（100 条/批），`QDataStream` 序列化通道值为 BLOB |
| **历史查询** | 按毫秒时间戳范围查询，`timestamp` 列建索引 |
| **自动清理** | 超 N 天数据自动 `DELETE`（默认 7 天） |
| **CSV 导出** | 时间范围 + 通道勾选 + `QFileDialog` 选择路径，Excel 兼容 |

### 界面

| 模块 | 实现方式 |
|------|------|
| **实时曲线** | `QWidget::paintEvent` + `QPolygonF::drawPolyline` 自绘，3 通道同时渲染 |
| **交互** | 滚轮缩放 X 轴（5s-120s）、鼠标拖拽平移、右键重置 |
| **性能优化** | 双缓冲 QPixmap 复用、`std::lower_bound` 可见区域裁剪、像素间距抽稀 |
| **数据表格** | `QAbstractTableModel` 子类化 + `QTableView`，定时器节流刷新（100ms） |
| **历史回放** | `QSlider` 时间轴 + 播放/暂停 + 1×/2×/5×/10× 速度调节 |
| **主窗口** | `QSplitter` 左右分屏（7:3）、菜单栏 + 工具栏 + 状态栏 |

### 架构

| 模块 | 实现方式 |
|------|------|
| **线程模型** | 通信线程（QTcpSocket I/O）→ 解析线程（协议解码 + 数据缓冲 + SQLite 写入）→ UI 线程（渲染） |
| **跨线程通信** | 全部使用 `Qt::QueuedConnection`，信号参数自动深拷贝 |
| **生命周期** | `closeEvent` 中 `quit() → wait() → deleteLater` 安全退出，无 QThread 警告 |

---

## 2. 技术栈

| 类别 | 选型 | 版本要求 |
|------|------|:--:|
| 语言 | C++ | 17 |
| UI 框架 | Qt (Core / Widgets / SerialPort / Network / Sql) | 6.5+ |
| 构建系统 | CMake | 3.20+ |
| 数据库 | SQLite 3 | —（Qt SQL 模块内置） |
| 编译器 | GCC / Clang / MSVC | C++17 支持 |
| 测试工具 | Python 3 | 3.8+（仅模拟器） |

---

## 3. 系统架构

### 分层结构

```
┌─────────────────────────────────────────────────────────┐
│                       UI 层 (src/ui/)                    │
│  MainWindow  RealTimeChart  HistoryPlayer  ExportDialog │
├─────────────────────────────────────────────────────────┤
│                    数据层 (src/data/)                    │
│  DataTableModel  DataBuffer  DatabaseManager            │
├─────────────────────────────────────────────────────────┤
│                    协议层 (src/protocol/)                │
│  Frame  ProtocolDecoder                                 │
├─────────────────────────────────────────────────────────┤
│                    线程层 (src/worker/)                  │
│  SerialWorker  TcpWorker  ParseWorker                   │
└─────────────────────────────────────────────────────────┘
```

### 数据流

```
TCP 模拟器 (100Hz)
  │ QTcpSocket::readyRead
  ▼
TcpWorker (通信线程)
  │ rawDataReceived → QueuedConnection
  ▼
ParseWorker (解析线程)
  ├── ProtocolDecoder::feed() → 状态机解码
  ├── DataBuffer::push()      → 环形缓冲
  ├── DatabaseManager::insert() → SQLite 批量写入
  ├── Heartbeat Timer          → 心跳检测
  └── emit dataPointReady()    → QueuedConnection
       │
       ▼
MainWindow (UI 线程)
  ├── DataTableModel → QTableView (10FPS)
  └── RealTimeChart  → paintEvent (25FPS)
```

---

## 4. 通信协议

### 帧格式（二进制，小端序）

```
┌──────────┬──────────┬──────────┬───────────┬───────────┐
│  Header  │  Length  │   Type   │  Payload  │   CRC16   │
│  2 bytes │  1 byte  │  1 byte  │  N bytes  │  2 bytes  │
├──────────┼──────────┼──────────┼───────────┼───────────┤
│ 0xA5 0x5A│  0..255  │  见下表  │   变长     │  CCITT    │
└──────────┴──────────┴──────────┴───────────┴───────────┘
```

### 帧类型

| Type | 名称 | Payload | 说明 |
|:----:|------|:--:|------|
| `0x01` | 数据帧 | 通道数据（默认 3×uint16） | 采集数据 |
| `0x02` | 心跳请求 | 空 | 上位机→下位机保活探测 |
| `0x03` | 心跳应答 | 空 | 下位机→上位机应答 |
| `0xFF` | 错误帧 | 错误码 + 描述 | 异常通知 |

### CRC16-CCITT

- 多项式：`0x1021`
- 初始值：`0xFFFF`
- 校验范围：Header + Length + Type + Payload（不含 CRC 自身）
- 标准测试向量：`crc16_ccitt("123456789", 9) == 0x29B1`

---

## 5. 本地构建

### 环境要求

| 组件 | 最低版本 | 说明 |
|------|:--:|------|
| CMake | 3.20 | 构建配置 |
| Qt | 6.5.0 | Core / Widgets / SerialPort / Network / Sql |
| MSVC 2022 / GCC 11 / Clang 14 | — | 需支持 C++17 |
| Python | 3.8 | 仅运行模拟器（可选） |

### 克隆与编译

```powershell
# Windows (MSVC + Qt Creator)
git clone https://gitee.com/kiyose408/pulse_qt.git
cd pulse_qt
# 使用 Qt Creator 打开 CMakeLists.txt → 配置 Desktop Qt 6.9.0 MSVC2022 64bit 套件 → 构建
```

```bash
# Linux
git clone https://gitee.com/kiyose408/pulse_qt.git && cd pulse_qt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 运行

```bash
./build/PulseQt    # Linux
build\Desktop_Qt_6_9_0_MSVC2022_64bit-Release\PulseQt.exe   # Windows
```

---

## 6. 测试方法

### 无需硬件 — TCP 模拟

```bash
# 终端 1：启动波形模拟器（正弦 + 锯齿 + 三角，100Hz）
python tools/tcp_wave_simulator.py

# 终端 2：启动 PulseQt，点击工具栏"连接"
```

### 验证功能

| 操作 | 预期结果 |
|------|------|
| 点"连接" | 状态栏"已连接"，左侧曲线开始滚动，右侧表格填充数据 |
| 点"停止" | 曲线冻结（TCP 不断），点"开始"恢复 |
| 点"断开" | 管道销毁，曲线定格 |
| 滚轮缩放 | X 轴时间范围 5s–120s |
| 拖拽平移 | 查看历史数据 |
| 右键 | 重置为 30s 默认视图 |
| 采集 1 分钟 → 拖动底部滑块 | 回放历史数据 |
| 文件 → 导出 CSV | 可选时间范围 + 通道，Excel 打开验证 |
| 点 X 关闭窗口 | 无 QThread 警告 |

---

## 7. 项目结构

```
PulseQt/
├── CMakeLists.txt
├── README.md
├── doc/                        设计文档
│   ├── 01_需求规格说明.md
│   ├── 02_架构设计.md
│   ├── 03_通信协议设计.md
│   └── 04_开发计划与日报.md
├── include/                    头文件
│   ├── MainWindow.h            RealTimeChart.h   HistoryPlayer.h
│   ├── DataTableModel.h        DataBuffer.h      DataPoint.h
│   ├── DatabaseManager.h       ExportDialog.h
│   ├── ProtocolDecoder.h       Frame.h
│   ├── ParseWorker.h           TcpWorker.h       SerialWorker.h
├── src/                        源码
│   ├── main.cpp
│   ├── ui/                     MainWindow / RealTimeChart / HistoryPlayer / ExportDialog
│   ├── data/                   DataBuffer / DataTableModel / DatabaseManager
│   ├── protocol/               ProtocolDecoder
│   ├── worker/                 ParseWorker / TcpWorker / SerialWorker
│   └── utils/                  Logger
├── tools/                      辅助工具
│   ├── tcp_wave_simulator.py   波形模拟器（正弦/锯齿/三角，推荐测试用）
│   ├── tcp_data_simulator.py   随机数据模拟器
│   ├── tcp_test_server.py      TCP 回环测试（开发用）
│   └── serial_test_server.py   串口测试（需虚拟串口对）
└── dist/                       发布产物（不上传 Git）
    └── PulseQt_Setup_v1.0.exe
```

---

## 8. 关键技术决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 曲线绘制 | `QPolygonF::drawPolyline` 替代 QChart | 100Hz 下 QChart 掉帧，自绘可控 |
| 抗锯齿 | 曲线关闭，坐标轴保留 | 软件渲染下抗锯齿极慢（400ms→5ms） |
| 线程模型 | `moveToThread` + `QueuedConnection` | 职责清晰，无锁设计，杜绝数据竞争 |
| SQLite 写入 | 批量事务（100 条/批） | 比逐条 INSERT 快 10–50 倍 |
| 二进制帧 | 自定义协议 + CRC16 | 位宽可控，无需 JSON/Protobuf 依赖 |
| 表格刷新 | 节流定时器（100ms） | 100Hz→10FPS，人眼够用，CPU 省 90% |

---

## 9. 性能指标

| 指标 | 实测值 | 说明 |
|------|:--:|------|
| 帧解码速率 | 100 Hz | 10ms/帧，状态机逐字节处理 |
| 曲线刷新率 | 25 FPS | QTimer 40ms，双缓冲防闪烁 |
| SQLite 写入 | ≥ 500 条/秒 | WAL + 批量事务 |
| 内存占用 | < 80 MB | 10000 条环形缓冲 + QPainter 双缓冲 |
| 断线感知 | ≤ 30s | 心跳 5s × 6 次 |

---

## 10. 许可证

MIT License — 详见 [dist/license.txt](dist/license.txt)

---

## 11. 开发者

**Kiyose** — C++ / Qt 开发者

- 仓库：https://gitee.com/kiyose408/pulse_qt
- 版本：v1.0（2026-06-05）
