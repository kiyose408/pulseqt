#include "ParseWorker.h"
#include "Frame.h"
#include "ModbusDecoder.h"
#include "ModbusMaster.h"
#include <QDateTime>
#include <QDebug>
#include <cstring>

ParseWorker::ParseWorker(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_dbManager.init(dbPath);
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(5000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ParseWorker::onHeartbeatCheck);
    m_heartbeatTimer->start();

    setProtocol("raw");
}

void ParseWorker::setProtocol(const QString &protocol)
{
    if (m_decoder) {
        disconnect(m_decoder, nullptr, this, nullptr);
        m_decoder = nullptr;
    }

    if (m_modbusMaster) {
        m_modbusMaster->stop();
        disconnect(m_modbusMaster, nullptr, this, nullptr);
    }

    if (protocol == "modbus") {
        if (!m_modbusDecoder)
            m_modbusDecoder = new ModbusDecoder(this);
        m_decoder = m_modbusDecoder;
        connect(m_modbusDecoder, &ModbusDecoder::frameDecoded,
                this, &ParseWorker::onFrameDecoded);

        if (!m_modbusMaster)
            m_modbusMaster = new ModbusMaster(1, 10, this);
        connect(m_modbusMaster, &ModbusMaster::writeData,
                this, &ParseWorker::writeData);
        m_modbusMaster->start();

        if (m_heartbeatTimer)
            m_heartbeatTimer->stop();
    } else {
        if (!m_rawDecoder)
            m_rawDecoder = new ProtocolDecoder(this);
        m_decoder = m_rawDecoder;
        connect(m_rawDecoder, &ProtocolDecoder::frameDecoded,
                this, &ParseWorker::onFrameDecoded);

        if (m_heartbeatTimer)
            m_heartbeatTimer->start();
    }
}

void ParseWorker::teardown()
{
    if (m_modbusMaster) {
        m_modbusMaster->stop();
        disconnect(m_modbusMaster, nullptr, this, nullptr);
    }
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    m_collecting = false;
    m_dbManager.flush();
}

void ParseWorker::onFrameDecoded(const Frame &frame)
{
    bool isModbus = (frame.type == 0x03 || frame.type == 0x04);
    bool isModbusEx = (frame.type == 0x83 || frame.type == 0x84);
    if (isModbusEx) {
        uint8_t excCode = frame.payload.size() >= 1
            ? static_cast<uint8_t>(frame.payload[0]) : 0;
        qWarning() << "Modbus exception: func" << Qt::hex << (frame.type & 0x7F)
                   << "code" << excCode;
        return;
    }
    if (!isModbus) {
        if (frame.type == Frame::TYPE_ACK) {
            m_heartbeatMissed = 0;
            return;
        }
        if (frame.type == Frame::TYPE_HEARTBEAT) {
            emit writeData(buildFrame(Frame::TYPE_ACK));
            return;
        }
        if (frame.type == Frame::TYPE_HANDSHAKE_REQ) {
            if (parseHandshakePayload(frame.payload)) {
                emit writeData(buildFrame(Frame::TYPE_HANDSHAKE_ACK,
                    QByteArray(1, '\x00')));
                emit handshakeCompleted(m_channelCount, m_channelTypes);
                qInfo() << "ParseWorker: handshake OK," << m_channelCount << "channels";
            } else {
                emit writeData(buildFrame(Frame::TYPE_HANDSHAKE_ACK,
                    QByteArray(1, '\x01')));
                qWarning() << "ParseWorker: handshake rejected";
            }
            return;
        }
        if (!m_collecting) return;
        if (frame.type != Frame::TYPE_DATA) return;
    }

    if (isModbus && !m_collecting) return;

    DataPoint dp;
    dp.timestamp = QDateTime::currentMSecsSinceEpoch();

    if (!parseDataPayload(frame.payload, dp, isModbus)) return;

    m_buffer.push(dp);
    m_dbManager.insert(dp);
    emit dataPointReady();
}

void ParseWorker::onRawDataReceived(const QByteArray &data)
{
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
    m_heartbeatMissed = 0;

    if (auto *raw = qobject_cast<ProtocolDecoder*>(m_decoder))
        raw->feed(data);
    else if (auto *mod = qobject_cast<ModbusDecoder*>(m_decoder))
        mod->feed(data);
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
        m_heartbeatTimer->stop();
        resetChannelConfig();
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
    if (count < 1 || count > 16) return false;
    if (payload.size() < 1 + count) return false;

    QVector<int> types;
    for (int i = 0; i < count; ++i) {
        uint8_t t = static_cast<uint8_t>(payload[1 + i]);
        if (Frame::channelTypeByteSize(t) == 0) return false;
        types.append(static_cast<int>(t));
    }

    m_channelCount = count;
    m_channelTypes = types;
    m_handshakeDone = true;
    return true;
}

bool ParseWorker::parseDataPayload(const QByteArray &payload, DataPoint &dp, bool modbus)
{
    if (modbus) {
        if (payload.size() < 3) return false;
        int byteCount = static_cast<uint8_t>(payload[0]);
        if (payload.size() < 1 + byteCount) return false;
        int chCount = byteCount / 2;
        dp.channels.reserve(chCount);
        auto d = reinterpret_cast<const uint8_t*>(payload.constData()) + 1;
        for (int i = 0; i < chCount; ++i)
            dp.channels.append(double((d[i*2] << 8) | d[i*2+1]));
        return true;
    }

    if (m_channelCount == 0) {
        if (payload.size() < 2) return false;
        int chCount = payload.size() / 2;
        dp.channels.reserve(chCount);
        auto d = reinterpret_cast<const uint8_t*>(payload.constData());
        for (int i = 0; i < chCount; ++i)
            dp.channels.append(double(d[i*2] | (d[i*2+1] << 8)));
        return true;
    }

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
    m_dbManager.flush();
}
