//==============================================================================
// ModbusDecoder — Modbus RTU 响应帧解码器
//
// 滑动CRC扫描：从 buffer 头部逐字节偏移，对每个偏移尝试帧长 4..256，
// 找到第一个有效 Modbus 帧（CRC 匹配 + 地址合法 + 功能码合法）即提取。
//==============================================================================

#ifndef MODBUSDECODER_H
#define MODBUSDECODER_H

#include <QObject>
#include <QByteArray>
#include "Frame.h"

class ModbusDecoder : public QObject
{
    Q_OBJECT

public:
    explicit ModbusDecoder(QObject *parent = nullptr);
    void feed(const QByteArray &data);

signals:
    void frameDecoded(const Frame &frame);

private:
    static uint16_t crc16_modbus(const uint8_t *data, size_t len);
    QByteArray m_buffer;
};

#endif // MODBUSDECODER_H
