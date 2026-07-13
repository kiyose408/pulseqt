//==============================================================================
// ModbusMaster 实现
//==============================================================================

#include "ModbusMaster.h"
#include "ModbusCRC.h"

// 构造 / 启停

ModbusMaster::ModbusMaster(uint8_t slaveAddr, int pollIntervalMs, QObject *parent)
    : QObject(parent), m_slave(slaveAddr)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(pollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ModbusMaster::onPoll);
}

void ModbusMaster::start()  { m_timer->start(); }
void ModbusMaster::stop()   { m_timer->stop(); }
bool ModbusMaster::isRunning() const { return m_timer->isActive(); }

// 定时轮询

void ModbusMaster::onPoll()
{
    emit writeData(buildReadHoldingRegisters(m_slave, m_startAddr, m_regCount));
}

// 构建读保持寄存器查询帧（功能码 0x03）

QByteArray ModbusMaster::buildReadHoldingRegisters(uint8_t slave,
                                                    uint16_t startAddr,
                                                    uint16_t regCount)
{
    QByteArray raw;
    raw.append(static_cast<char>(slave));
    raw.append(static_cast<char>(0x03));
    raw.append(static_cast<char>((startAddr >> 8) & 0xFF));
    raw.append(static_cast<char>(startAddr & 0xFF));
    raw.append(static_cast<char>((regCount >> 8) & 0xFF));
    raw.append(static_cast<char>(regCount & 0xFF));

    uint16_t crc = crc16_modbus(
        reinterpret_cast<const uint8_t*>(raw.constData()), raw.size());
    raw.append(static_cast<char>(crc & 0xFF));
    raw.append(static_cast<char>((crc >> 8) & 0xFF));
    return raw;
}
