#include "ParseWorker.h"
#include <QDateTime>
#include <QDebug>

extern uint16_t crc16_ccitt(const uint8_t *data, size_t len);

ParseWorker::ParseWorker(const QString &dbPath, QObject *parent): QObject(parent)
{
    m_dbManager.init(dbPath);
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(5000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ParseWorker::onHeartbeatCheck);
    m_heartbeatTimer->start();

    connect(&m_decoder, &ProtocolDecoder::frameDecoded, this,
            [this](const Frame &frame) {

        // 心跳应答 — 设备回了我们的心跳
        if (frame.type == Frame::TYPE_ACK) {
            m_heartbeatMissed = 0;
            return;
        }

        // 心跳请求 — 设备发来的心跳，回一个应答
        if (frame.type == Frame::TYPE_HEARTBEAT) {
            emit writeData(buildFrame(Frame::TYPE_ACK));
            return;
        }

        if (!m_collecting) return;
                if (frame.type != Frame::TYPE_DATA || frame.payload.size() < 6) return;

                DataPoint dp;
                dp.timestamp = QDateTime::currentMSecsSinceEpoch();
                auto d = reinterpret_cast<const uint8_t*>(frame.payload.constData());
                dp.channels = {
                    double(d[0] | (d[1] << 8)),
                    double(d[2] | (d[3] << 8)),
                    double(d[4] | (d[5] << 8))
                };

                m_buffer.push(dp);
                m_dbManager.insert(dp);
                emit dataPointReady();   // 通知 UI
            });
}
void ParseWorker::onRawDataReceived(const QByteArray &data)
{
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
    m_heartbeatMissed = 0;
    m_decoder.feed(data);
}

void ParseWorker::setCollecting(bool on)
{
    m_collecting = on;
}

QByteArray ParseWorker::buildFrame(uint8_t type, const QByteArray &payload)
{
    QByteArray raw;
    raw.append('\xA5'); raw.append('\x5A');
    raw.append(static_cast<char>(payload.size()));
    raw.append(static_cast<char>(type));
    raw.append(payload);

    uint16_t crc = crc16_ccitt(reinterpret_cast<const uint8_t*>(raw.constData()), raw.size());
    raw.append(static_cast<char>(crc & 0xFF));
    raw.append(static_cast<char>((crc >> 8) & 0xFF));
    return raw;
}

void ParseWorker::onHeartbeatCheck()
{
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastDataTime;

    if (elapsed >= 5000) {
        emit writeData(buildFrame(Frame::TYPE_HEARTBEAT));
        m_heartbeatMissed++;
        qInfo() << "Heartbeat sent, missed:" << m_heartbeatMissed;
    }

    if (m_heartbeatMissed >= 6) {
        qWarning() << "Heartbeat timeout (30s)";
        m_heartbeatTimer->stop();                          // 停止继续发
    }
}
DataBuffer *ParseWorker::buffer()
{
    return &m_buffer;
}

DatabaseManager *ParseWorker::dbManager()
{
    return &m_dbManager;
}
ParseWorker::~ParseWorker()
{
    m_dbManager.flush();   // 提交缓冲的最后一波数据（不到 100 条也落盘）
}
