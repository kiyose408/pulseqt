//==============================================================================
// Frame - 通信帧数据结构
//
// 一个完整的解码帧，包含帧类型和 payload 数据。
// 解码器（ProtocolDecoder）从字节流中提取 Frame，上层根据 type 解析 payload。
//
// 帧格式（二进制，小端序）：
//   Header(2B) + Length(1B) + Type(1B) + Payload(NB) + CRC16(2B)
//   0xA5 0x5A  +  0~255    + 见下方常量 +  变长       + CCITT
//==============================================================================

#ifndef FRAME_H
#define FRAME_H

#include <cstdint>
#include <cstddef>
#include <QByteArray>
#include <QMetaType>
#include <cstddef>      //size_t

//CRC16-CCITT 查表法(多项式0x1021,初始值0xFFFF)
//标准测试向量：crc16_ccitt("123456789",9) == 0x29B1

uint16_t crc16_ccitt(const uint8_t *data, size_t len);
struct Frame
{
    static constexpr uint16_t HEADER = 0xA55A;    // 帧同步头

    uint8_t     type    = 0;         // 帧类型（见下方常量）
    QByteArray  payload;             // 数据负载（原始字节）

    // ── 帧类型常量 ────────────────────────────────────────────────
    static constexpr uint8_t TYPE_DATA      = 0x01;  // 数据帧
    static constexpr uint8_t TYPE_HEARTBEAT = 0x02;  // 心跳帧（保活）
    static constexpr uint8_t TYPE_ACK       = 0x03;  // 心跳应答
    static constexpr uint8_t TYPE_ERROR     = 0xFF;  // 错误帧
};

// 使 Frame 可被 QSignalSpy 捕获（QtTest / 跨线程信号）
Q_DECLARE_METATYPE(Frame)

// CRC16-CCITT 查表法（多项式 0x1021，初始值 0xFFFF）
// 标准测试向量：crc16_ccitt("123456789", 9) == 0x29B1
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

#endif // FRAME_H
