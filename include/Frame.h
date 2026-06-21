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

struct Frame
{
    static constexpr uint16_t HEADER = 0xA55A;    // 帧同步头

    uint8_t     type    = 0;         // 帧类型（见下方常量）
    QByteArray  payload;             // 数据负载（原始字节）

    // ── 帧类型常量 ────────────────────────────────────────────────
    static constexpr uint8_t TYPE_DATA           = 0x01;  // 数据帧
    static constexpr uint8_t TYPE_HEARTBEAT      = 0x02;  // 心跳帧（保活）
    static constexpr uint8_t TYPE_ACK            = 0x03;  // 心跳应答
    static constexpr uint8_t TYPE_HANDSHAKE_REQ  = 0x04;  // 握手请求（下位机→上位机：通道配置）
    static constexpr uint8_t TYPE_HANDSHAKE_ACK  = 0x05;  // 握手应答（上位机→下位机：0x00=OK）
    static constexpr uint8_t TYPE_ERROR          = 0xFF;  // 错误帧

    // ── 通道数据类型编码（握手帧 payload 中使用）────────────────
    enum ChannelDataType : uint8_t {
        CH_UINT8  = 0x01,  // 1 字节，无符号 0~255
        CH_UINT16 = 0x02,  // 2 字节，无符号 0~65535（默认）
        CH_INT16  = 0x03,  // 2 字节，有符号 -32768~32767
        CH_FLOAT  = 0x04   // 4 字节，IEEE 754 单精度
    };

    // 返回通道类型对应的字节数，未知类型返回 0
    static inline int channelTypeByteSize(uint8_t type) {
        switch (type) {
            case CH_UINT8:  return 1;
            case CH_UINT16: return 2;
            case CH_INT16:  return 2;
            case CH_FLOAT:  return 4;
            default:        return 0;
        }
    }
};

// 使 Frame 可被 QSignalSpy 捕获（QtTest / 跨线程信号）
Q_DECLARE_METATYPE(Frame)

// CRC16-CCITT 查表法（多项式 0x1021，初始值 0xFFFF）
// 标准测试向量：crc16_ccitt("123456789", 9) == 0x29B1
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

#endif // FRAME_H
