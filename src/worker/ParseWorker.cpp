#include "ParseWorker.h"
#include "Frame.h"
#include <QDateTime>
#include <QDebug>
#include <cstring>

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

        // 握手请求 — 下位机告知通道配置
        if (frame.type == Frame::TYPE_HANDSHAKE_REQ) {
            if (parseHandshakePayload(frame.payload)) {
                emit writeData(buildFrame(Frame::TYPE_HANDSHAKE_ACK,
                    QByteArray(1, '\x00')));  // result=0x00 OK
                emit handshakeCompleted(m_channelCount, m_channelTypes);
                qInfo() << "ParseWorker: handshake OK," << m_channelCount << "channels";
            } else {
                emit writeData(buildFrame(Frame::TYPE_HANDSHAKE_ACK,
                    QByteArray(1, '\x01')));  // result=0x01 不支持
                qWarning() << "ParseWorker: handshake rejected (unsupported config)";
            }
            return;
        }

        if (!m_collecting) return;
                if (frame.type != Frame::TYPE_DATA) return;

                DataPoint dp;
                dp.timestamp = QDateTime::currentMSecsSinceEpoch();

                if (!parseDataPayload(frame.payload, dp)) return;

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
        resetChannelConfig();                              // 下次重连需重新握手
    }
}

void ParseWorker::resetChannelConfig()
{
    m_channelCount = 0;
    m_channelTypes.clear();
    m_handshakeDone = false;
}

bool ParseWorker::parseHandshakePayload(const QByteArray &payload)
{
    if (payload.size() < 1) return false;

    int count = static_cast<uint8_t>(payload[0]);
    if (count < 1 || count > 16) return false;          // 限制 1~16 通道
    if (payload.size() < 1 + count) return false;       // payload 不够

    QVector<int> types;
    for (int i = 0; i < count; ++i) {
        uint8_t t = static_cast<uint8_t>(payload[1 + i]);
        if (Frame::channelTypeByteSize(t) == 0) return false;  // 不支持的类型
        types.append(static_cast<int>(t));
    }

    m_channelCount = count;
    m_channelTypes = types;
    m_handshakeDone = true;
    return true;
}

bool ParseWorker::parseDataPayload(const QByteArray &payload, DataPoint &dp)
{
    if (m_channelCount == 0) {
        // 未握手：回退到默认行为（uint16 通道，payload 长度 ÷ 2）
        if (payload.size() < 2) return false;
        int chCount = payload.size() / 2;
        dp.channels.reserve(chCount);
        auto d = reinterpret_cast<const uint8_t*>(payload.constData());
        for (int i = 0; i < chCount; ++i)
            dp.channels.append(double(d[i*2] | (d[i*2+1] << 8)));
        return true;
    }

    // 已握手：按协商的类型逐通道解析
    auto d = reinterpret_cast<const uint8_t*>(payload.constData());
    int offset = 0;
    dp.channels.reserve(m_channelCount);

    for (int i = 0; i < m_channelCount; ++i) {
        int sz = Frame::channelTypeByteSize(static_cast<uint8_t>(m_channelTypes[i]));
        if (offset + sz > payload.size()) return false;

        double val = 0.0;
        switch (m_channelTypes[i]) {
        case Frame::CH_UINT8:
            val = double(d[offset]);
            break;
        case Frame::CH_UINT16:
            val = double(d[offset] | (d[offset+1] << 8));
            break;
        case Frame::CH_INT16: {
            int16_t raw = int16_t(d[offset] | (d[offset+1] << 8));
            val = double(raw);
            break;
        }
        case Frame::CH_FLOAT: {
            uint32_t bits = d[offset] | (d[offset+1] << 8)
                          | (d[offset+2] << 16) | (d[offset+3] << 24);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            val = double(f);
            break;
        }
        default: return false;
        }
        dp.channels.append(val);
        offset += sz;
    }
    return true;
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
