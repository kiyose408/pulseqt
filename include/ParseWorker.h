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
    void setProtocol(const QString &protocol);
    void teardown();

signals:
    void dataPointReady();
    void writeData(const QByteArray &data);
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
    DataBuffer       m_buffer{10000};
    DatabaseManager  m_dbManager;

    int m_channelCount = 0;
    QVector<int> m_channelTypes;
    bool m_handshakeDone = false;
};

#endif // PARSEWORKER_H
