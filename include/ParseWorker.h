//==============================================================================
// ParseWorker — 多线程解析核心
//==============================================================================
//
// 运行在独立 QThread，串联数据处理链路：
//   字节流 → 协议解码 → FilterPipeline → DataBuffer → SQLite
//
// 双协议：setProtocol("raw"|"modbus") 运行时切换。
//   raw:   ProtocolDecoder (7 状态机) + 心跳保活
//   modbus: ModbusDecoder (滑动 CRC) + ModbusMaster (定时轮询)
//==============================================================================

#ifndef PARSEWORKER_H
#define PARSEWORKER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QDateTime>
#include "ProtocolDecoder.h"
#include "DataBuffer.h"
#include "DatabaseManager.h"
#include "FilterPipeline.h"

class ModbusDecoder;
class ModbusMaster;

/// @brief 解析线程 Worker — 解码 + 过滤 + 缓冲 + 落库
///
/// 通过 moveToThread 运行在独立 QThread。
/// 所有槽函数通过 QueuedConnection 被跨线程调用。
class ParseWorker : public QObject
{
    Q_OBJECT
public:
    /// @brief 构造，指定 SQLite 数据库路径
    /// @param dbPath 数据库文件路径，默认 "data.db"
    explicit ParseWorker(const QString &dbPath = "data.db",
                         QObject *parent = nullptr);
    ~ParseWorker();

    /// @brief 返回数据环形缓冲（UI 线程通过 snapshot 读取）
    DataBuffer *buffer();
    /// @brief 返回数据库管理器
    DatabaseManager *dbManager();
    /// @brief 返回过滤器管道（运行时配置过滤器）
    FilterPipeline *pipeline();

public slots:
    /// @brief 接收原始字节（来自 ChannelManager，跨线程 QueuedConnection）
    /// @param data 原始 TCP/串口字节流
    void onRawDataReceived(const QByteArray &data);

    /// @brief 控制采集启停
    /// @param on true=开始采集，false=暂停
    void setCollecting(bool on);

    /// @brief 心跳超时检查（m_heartbeatTimer 定时触发）
    void onHeartbeatCheck();

    /// @brief 重置通道配置（断开连接时调用）
    void resetChannelConfig();

    /// @brief 运行时切换协议
    /// @param protocol "raw"=自定义二进制协议, "modbus"=Modbus RTU
    void setProtocol(const QString &protocol);

    /// @brief 安全关闭：停止定时器 + 提交缓冲 + 断开 Modbus
    void teardown();

signals:
    /// @brief 新数据点已写入缓冲，UI 线程应刷新曲线/表格
    void dataPointReady();

    /// @brief 向 ChannelManager 发送数据（Modbus 查询帧、心跳应答等）
    /// @param data 待发送字节
    void writeData(const QByteArray &data);

    /// @brief 握手完成，通知 UI 通道数量与类型
    /// @param channelCount 通道数量
    /// @param types 各通道数据类型（0x01=uint8, 0x02=uint16, 0x04=float）
    void handshakeCompleted(int channelCount, const QVector<int> &types);

private:
    void onFrameDecoded(const Frame &frame);
    QByteArray buildFrame(uint8_t type, const QByteArray &payload = {});
    bool parseHandshakePayload(const QByteArray &payload);
    bool parseDataPayload(const QByteArray &payload, DataPoint &dp, bool modbus = false);

    QObject        *m_decoder    = nullptr;
    ProtocolDecoder *m_rawDecoder = nullptr;
    ModbusDecoder   *m_modbusDecoder = nullptr;
    ModbusMaster    *m_modbusMaster  = nullptr;
    bool m_collecting = false;
    QTimer *m_heartbeatTimer = nullptr;
    qint64  m_lastDataTime   = 0;
    int     m_heartbeatMissed = 0;
    DataBuffer       m_buffer;
    DatabaseManager  m_dbManager;
    FilterPipeline   m_pipeline;

    int m_channelCount = 0;
    QVector<int> m_channelTypes;
    bool m_handshakeDone = false;
};

#endif // PARSEWORKER_H
