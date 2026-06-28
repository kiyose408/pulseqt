#ifndef PARSEWORKER_H
#define PARSEWORKER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QDateTime>
#include "ProtocolDecoder.h"
#include "DataBuffer.h"
#include "DatabaseManager.h"

class ModbusDecoder;
class ModbusMaster;

class ParseWorker : public QObject
{
    Q_OBJECT
public:
    explicit ParseWorker(const QString &dbPath = "data.db",
                         const QString &protocol = "raw",
                         QObject *parent = nullptr);
    ~ParseWorker();
    DataBuffer *buffer();
    DatabaseManager *dbManager();

public slots:
    void onRawDataReceived(const QByteArray &data);
    void setCollecting(bool on);
    void onHeartbeatCheck();
    void resetChannelConfig();
    void setProtocol(const QString &protocol);  // "raw" / "modbus"

signals:
    void dataPointReady();
    void writeData(const QByteArray &data);
    void handshakeCompleted(int channelCount, const QVector<int> &types);

private:
    void onFrameDecoded(const Frame &frame);  // 统一的帧处理（与当前 decoder 解耦）
    QByteArray buildFrame(uint8_t type, const QByteArray &payload = {});
    bool parseHandshakePayload(const QByteArray &payload);
    bool parseDataPayload(const QByteArray &payload, DataPoint &dp, bool modbus = false);
    QObject        *m_decoder    = nullptr;  // 当前活跃的解码器
    ProtocolDecoder *m_rawDecoder = nullptr;  // 自定义协议
    ModbusDecoder   *m_modbusDecoder = nullptr;  // Modbus RTU
    ModbusMaster    *m_modbusMaster  = nullptr;  // Modbus 主站轮询
    bool m_collecting = false;
    QTimer *m_heartbeatTimer = nullptr;
    qint64  m_lastDataTime   = 0;
    int     m_heartbeatMissed = 0;
    DataBuffer       m_buffer{10000};
    DatabaseManager  m_dbManager;

    // ── 握手协商的通道配置 ───────────────────────────
    int m_channelCount = 0;                     // 通道数（0=未握手，回退默认行为）
    QVector<int> m_channelTypes;                // 每通道的数据类型编码
    bool m_handshakeDone = false;               // 握手是否已完成

    //三个成员都是值对象（不是指针 new）
    //，随 ParseWorker 一起 moveToThread，自动归属解析线程。不用手动管理生命周期。
};

#endif // PARSEWORKER_H
