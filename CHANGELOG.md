# Changelog

## v1.1.1 (2026-06-21)

### 🔌 协议层：握手帧 (T018 完成)

- 实现握手请求 (0x04) / 握手应答 (0x05) — 下位机连接后主动声明通道配置
- `Frame::ChannelDataType` 枚举支持 uint8 / uint16 / int16 / float 四种通道类型
- `ParseWorker::parseHandshakePayload()`：校验 1~16 通道 + 类型有效性，握手失败回复 0x01
- `ParseWorker::parseDataPayload()`：按协商类型逐通道解析，`m_channelCount==0` 时回退旧行为
- 心跳超时自动调用 `resetChannelConfig()`，断线重连强制重新握手
- `DataTableModel::setChannelCount()`：握手后动态调整表格列数
- `MainWindow` 状态栏显示 "已连接 · N 通道"
- 向后兼容：旧版模拟器不发握手帧时行为完全不变

### 🧪 新增测试

- TestParseWorker 新增 8 个握手测试用例（握手解析 / uint16解析 / float解析 / 混合类型 / 拒绝零通道 / 拒绝未知类型 / 拒绝超16通道 / reset回退）
- 总计 53 单元测试 · 7 二进制

---

## v1.1.0 (2026-06-20)

### 🏗 架构重构

- **统一为 IChannel + ChannelManager 架构**，移除 TcpWorker/SerialWorker 双架构并存
- IChannel 虚接口多态支持 TCP (TcpChannel) 和串口 (SerialChannel)，通过 ChannelRegistry 注册表动态选择
- 连接对话框 (ConnectionDialog) 由注册表驱动，新增通道类型只需 3 行注册代码
- 三线程模型：UI 线程 / 通信线程 (ChannelManager+IChannel) / 解析线程 (ParseWorker)

### 📊 可视化升级

- **双图表架构**：上方实时曲线 + 下方独立回放曲线，各自绑定独立 DataBuffer，互不抢占
- Y 轴自适应：从可见数据动态计算范围，支持 12-bit ADC 等非标准量程
- Y 轴刻度与曲线同步对齐（computeYRange 预计算）
- X 轴墙钟驱动：无数据时曲线持续左移不卡顿
- 回放窗口自动匹配数据跨度：大窗口大数据不再留白
- 拖拽方向修正：右拖看新数据，左拖看旧数据
- 数据表列宽均分 (Stretch 模式)，所有通道完整可见
- **暗色主题**：Fusion + Palette 全局覆盖，图表 QPainter 自绘适配

### 🧪 测试体系 (45 用例 · 7 二进制)

- T022 TestFrame + TestProtocolDecoder (10 用例)
- T023 TestDataBuffer (8 用例，含多线程压测)
- T024 TestDatabaseManager (10 用例)
- T025 TestIntegration (2 用例 · 全链路 TCP 采集 + 断线重连)
- T026 GitHub Actions CI (Ubuntu + Windows 双平台)
- TestChannelManager (9 用例 · 信号转发/断开守卫/线程安全关闭)
- TestChannelRegistry (5 用例 · 注册去重/工厂/null 保护)
- TestParseWorker (7 用例 · 帧解析/心跳/collecting 切换)

### 🐛 Bug 修复 (20 项)

- 修复 CRC16-CCITT 重复声明
- 修复 valueToPixelY 除零风险
- 修复集成测试线程析构崩溃 (BlockingQueuedConnection)
- 修复 connectToDevice 跨线程静默失败 (→ public slots)
- 修复 IChannel 未设为 ChannelManager 子对象 (→ setParent)
- 修复 onDisconnect 全拆导致历史回放不可用
- 修复 View 菜单 action 无 connect
- 修复外部 extern 声明替代 Frame.h include
- 修复 killStaleSimulators Windows 专用 (→ 跨平台)
- 修复 SerialWorker 死代码残留 (→ 物理删除)
- 修复 Y 轴硬编码 0-1024 (→ 自适应)
- 修复 帧解码 qInfo 日志洪水 (→ qDebug)
- 修复 断开时崩溃 (TcpChannel::close 先 disconnect 再 abort)
- 修复 图表拖拽方向反直觉
- 修复 数据表列宽不均衡
- 修复 Y 轴刻度不同步 (computeYRange 预计算)
- 修复 setChannel 旧通道泄漏 (→ deleteLater)
- 修复 心跳逻辑审查误判 (经复核逻辑正确)
- 修复 墙钟时间 uint64_t 回拨风险 (→ qint64 + 守卫)
- 修复 teardown deleteLater 可能不被处理 (→ BlockingQueuedConnection)

### 📝 文档

- Bug 修复日志 (20 项，含现象/根因/修复/教训)
- 单元测试设计方法与实施专题
- 集成测试设计方法与实施专题
- 测试体系建设总结与 Bug 发掘
- README 更新至 v1.1 架构描述

---

## v1.0.0 (2026-06-05)

- 首次发布
- Qt 6.5 / C++17 / CMake
- TCP 数据采集 · 自定义二进制协议 · 7 状态机解码
- QPainter 自绘实时曲线 · QAbstractTableModel 数据表格
- SQLite WAL + 批量事务 · 历史回放 · CSV 导出
- 三线程架构 · 心跳保活 · 指数退避重连
- 4 个 Python 模拟器工具
- NSIS 安装包
