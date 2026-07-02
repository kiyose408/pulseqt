//==============================================================================
// ProtocolDecoder 实现
//
// 核心算法：
//   1. CRC16-CCITT 查表法（多项式 0x1021，初始值 0xFFFF）
//   2. 状态机逐字节解码，支持粘包/拆包
//
// ⚠️ 本任务遇到的关键 Bug（开发日志 T005 有详细记录）：
//   ① m_crcReceived = byte << 8（覆盖低字节）→ 改为 |= 拼合
//   ② 空 payload 帧残留上一帧数据 → 两处清空 m_payload
//   ③ uint8_t m_crcReceived 溢出 → 改为 uint16_t
//   ④ frameDecoder 拼写 → frameDecoded
//   ⑤ reset() 声明未实现 → LNK2019 链接错误
//==============================================================================

#include "ProtocolDecoder.h"
#include <QDebug>

//==============================================================================
// CRC16-CCITT 查表（多项式 0x1021）
// 标准测试向量：crc16_ccitt("123456789", 9) == 0x29B1
//==============================================================================

const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;   // 初始值
    for (size_t i = 0; i < len; ++i) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

//==============================================================================
// 状态机重置
//==============================================================================

void ProtocolDecoder::reset()
{
    m_state       = WAIT_HEADER_H;
    m_buffer.clear();
    m_payload.clear();
    m_crcReceived = 0;
    m_payloadLen  = 0;
    m_frameType   = 0;
    m_payloadIdx  = 0;
}

//==============================================================================
// 逐字节解码 — 7 状态状态机
//
// 状态流转（每个字节触发一次状态转换）：
//   WAIT_HEADER_H  ─→ 找到 0xA5 ─→ WAIT_HEADER_L
//   WAIT_HEADER_L  ─→ 找到 0x5A ─→ WAIT_LENGTH   （找错→回 HEADER_H）
//   WAIT_LENGTH    ─→ 读长度     ─→ WAIT_TYPE
//   WAIT_TYPE      ─→ 读类型     ─→ WAIT_PAYLOAD  （或 length=0→CRC_L）
//   WAIT_PAYLOAD   ─→ 读 N 字节  ─→ WAIT_CRC_L
//   WAIT_CRC_L     ─→ 存低字节   ─→ WAIT_CRC_H
//   WAIT_CRC_H     ─→ 拼高字节 → CRC 校验 → emit / 丢弃 → 回 HEADER_H
//==============================================================================

void ProtocolDecoder::feed(const QByteArray &data)
{
    // 逐字节推进（粘包时一次 feed 可能解出多帧）
    for (int i = 0; i < data.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(data[i]);

        switch (m_state) {

        case WAIT_HEADER_H:
            if (byte == 0xA5) {
                m_buffer.clear();           // 开始新帧
                m_buffer.append(byte);      // 记录字节（用于 CRC）
                m_state = WAIT_HEADER_L;
            }
            // 非 0xA5 → 跳过（垃圾数据过滤）
            break;

        case WAIT_HEADER_L:
            if (byte == 0x5A) {
                m_buffer.append(byte);      // 帧头确认
                m_payload.clear();          // ⚠️ 清空上一帧残留（空 payload 帧不会进 WAIT_PAYLOAD）
                m_state = WAIT_LENGTH;
            } else {
                // 假帧头：0xA5 后面不是 0x5A → 回到起点
                m_state = WAIT_HEADER_H;
            }
            break;

        case WAIT_LENGTH:
            m_payloadLen = byte;            // Payload 字节数（0~255）
            m_buffer.append(byte);
            m_state = WAIT_TYPE;
            break;

        case WAIT_TYPE:
            m_frameType = byte;
            m_buffer.append(byte);
            if (m_payloadLen == 0) {
                m_payload.clear();          // ⚠️ 空帧也要清空 payload
                m_state = WAIT_CRC_L;       // 跳过 WAIT_PAYLOAD，直接等 CRC
            } else {
                m_payloadIdx = 0;
                m_payload.clear();
                m_state = WAIT_PAYLOAD;
            }
            break;

        case WAIT_PAYLOAD:
            m_payload.append(byte);         // 累积 payload 字节
            m_buffer.append(byte);          // 同步记录到 CRC 缓冲区
            m_payloadIdx++;
            if (m_payloadIdx >= m_payloadLen)
                m_state = WAIT_CRC_L;       // Payload 收齐，进入 CRC 阶段
            break;

        case WAIT_CRC_L:
            m_crcReceived = byte;           // CRC 低字节（小端序先发低位）
            m_state = WAIT_CRC_H;
            break;

        case WAIT_CRC_H:
            // ⚠️ 用 |= 拼合，不能 = 覆盖（会丢失 WAIT_CRC_L 存的低字节）
            m_crcReceived |= (static_cast<uint16_t>(byte) << 8);

            // 对 m_buffer（Header+Length+Type+Payload）计算 CRC
            uint16_t crcCalc = crc16_ccitt(
                reinterpret_cast<const uint8_t*>(m_buffer.constData()),
                m_buffer.size());

            if (crcCalc == m_crcReceived) {
                // ✅ 校验通过 → 组装 Frame → 发射信号
                Frame frame;
                frame.type    = m_frameType;
                frame.payload = m_payload;

                emit frameDecoded(frame);
            } else {
                // ❌ CRC 失败 → 限流日志，防止 Fuzzing 洪水
                static int crcErrCount = 0;
                if (++crcErrCount <= 3)
                    qWarning() << "CRC mismatch: calc" << Qt::hex << crcCalc
                               << "recv" << m_crcReceived;
                emit crcError(m_buffer);
            }

            // 无论成败，回到起点等待下一帧
            m_state = WAIT_HEADER_H;
            break;
        }
    }
}
