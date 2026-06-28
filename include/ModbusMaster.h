//==============================================================================
// ModbusMaster — Modbus RTU 主站（主动轮询从站）
//
// 使用 QTimer 定时发送查询帧（功能码 0x03 读保持寄存器）。
// 查询帧: Slave(1) + Func(1) + StartAddr(2) + RegCount(2) + CRC(2) = 8 bytes
// writeData 信号连接到 ParseWorker → ChannelManager → IChannel
//==============================================================================

#ifndef MODBUSMASTER_H
#define MODBUSMASTER_H

#include <QObject>
#include <QTimer>

class ModbusMaster : public QObject
{
    Q_OBJECT

public:
    // slaveAddr: 从站地址 (1-247)
    // pollIntervalMs: 轮询间隔 (ms)，通常 10ms = 100Hz
    explicit ModbusMaster(uint8_t slaveAddr = 1,
                          int pollIntervalMs = 10,
                          QObject *parent = nullptr);

    void start();
    void stop();
    bool isRunning() const;

    static QByteArray buildReadHoldingRegisters(uint8_t slave,
                                                 uint16_t startAddr,
                                                 uint16_t regCount);

signals:
    void writeData(const QByteArray &data);

private slots:
    void onPoll();

private:
    QTimer  *m_timer  = nullptr;
    uint8_t  m_slave;
    uint16_t m_startAddr = 0;
    uint16_t m_regCount  = 3;  // 3 通道
};

#endif // MODBUSMASTER_H
