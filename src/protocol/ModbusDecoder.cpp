//==============================================================================
// ModbusDecoder 实现
//==============================================================================

#include "ModbusDecoder.h"
#include "ModbusCRC.h"

ModbusDecoder::ModbusDecoder(QObject *parent) : QObject(parent) {}

void ModbusDecoder::feed(const QByteArray &data)
{
    m_buffer.append(data);

    while (m_buffer.size() >= 4) {
        const uint8_t *raw = reinterpret_cast<const uint8_t*>(m_buffer.constData());
        int bufLen = m_buffer.size();
        bool found = false;

        for (int offset = 0; offset < bufLen - 3 && !found; ++offset) {
            int maxFrame = bufLen - offset;
            if (maxFrame > 256) maxFrame = 256;

            for (int frameLen = 4; frameLen <= maxFrame; ++frameLen) {
                uint16_t crcCalc = crc16_modbus(raw + offset, frameLen - 2);
                uint16_t crcRecv = (raw[offset + frameLen - 1] << 8)
                                 |  raw[offset + frameLen - 2];
                if (crcCalc != crcRecv) continue;

                uint8_t addr = raw[offset];
                uint8_t func = raw[offset + 1];
                if (addr < 1 || addr > 247) continue;
                if ((func > 0x10 && func < 0x80) || func > 0x8F) continue;

                Frame frame;
                frame.type    = func;
                frame.payload = m_buffer.mid(offset + 2, frameLen - 4);
                emit frameDecoded(frame);
                m_buffer.remove(0, offset + frameLen);
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    static constexpr int MAX_BUFFER  = 2048;
    static constexpr int TRIM_TARGET = 1024;
    if (m_buffer.size() > MAX_BUFFER)
        m_buffer.remove(0, m_buffer.size() - TRIM_TARGET);
}
