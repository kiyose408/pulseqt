//==============================================================================
// MagCollectorFrame — 六通道磁场集采器通信帧数据结构
//
// 帧格式（二进制，小端序，固定 48 字节）：
//   帧头(2B) + 帧长(2B) + 序列(4B) + 时间(12B) + 6×(int24+标志)(24B) + 校验和(2B) + 帧尾(2B)
//
//   0      2     帧头: 0x46 0x4D ("FM")
//   2      2     帧总长: uint16 LE (=48)
//   4      4     帧序列: uint32 LE
//   8      1     周 (WW)
//   9      1     月 (MM)
//  10      1     日 (DD)
//  11      1     年 (YY, 0~99)
//  12      1     时 (HH)
//  13      1     分 (MM)
//  14      1     秒 (SS)
//  15      1     时间格式 (FF)
//  16      4     毫秒: uint32 LE
//  20      3     X1/Y1/Z1/X2/Y2/Z2 数据: int24 LE (各 3B)
//  23/27/31/35/39/43  1    通道标志: 'X','Y','Z','x','y','z'
//  44      2     校验和: uint16 LE (覆盖 offset 4~43 共 40 字节的字节和)
//  46      2     帧尾: 0x0D 0x0A (CR LF)
//
// 校验和算法：去除帧头、帧尾、帧长度字段后，剩余 40 字节的简单无符号字节累加。
//==============================================================================

#ifndef MAGCOLLECTORFRAME_H
#define MAGCOLLECTORFRAME_H

#include <cstdint>
#include <QByteArray>

struct MagCollectorFrame
{
    // ── 帧常量 ──────────────────────────────────────────────────
    static constexpr uint8_t  HEADER_H       = 0x46;   // 'F'
    static constexpr uint8_t  HEADER_L       = 0x4D;   // 'M'
    static constexpr uint8_t  FOOTER_H       = 0x0D;   // CR
    static constexpr uint8_t  FOOTER_L       = 0x0A;   // LF
    static constexpr int      CHANNEL_COUNT  = 6;
    static constexpr int      RAW_FRAME_SIZE = 48;     // 帧总字节数
    static constexpr int      PAYLOAD_SIZE   = 40;     // 校验覆盖字节数 (offset 4~43)

    // 通道标志 ASCII 码（用于帧完整性校验）
    static constexpr uint8_t CH_FLAG_X1 = 0x58;  // 'X'
    static constexpr uint8_t CH_FLAG_Y1 = 0x59;  // 'Y'
    static constexpr uint8_t CH_FLAG_Z1 = 0x5A;  // 'Z'
    static constexpr uint8_t CH_FLAG_X2 = 0x78;  // 'x'
    static constexpr uint8_t CH_FLAG_Y2 = 0x79;  // 'y'
    static constexpr uint8_t CH_FLAG_Z2 = 0x7A;  // 'z'

    // ── 帧字段 ──────────────────────────────────────────────────
    uint16_t  frameLength = 0;       // 帧总长（当前恒为 48）
    uint32_t  sequence    = 0;       // 帧序列号（递增）
    uint8_t   timestamp[12] = {};    // 时间信息 (WW/MM/DD/YY/HH/MM/SS/FF/MS_4B)
    int32_t   data[6]     = {};      // 6 通道 int24 数据（符号扩展到 int32）
    uint8_t   flags[6]    = {};      // 通道标志位 ('X','Y','Z','x','y','z')
    uint16_t  checksum    = 0;       // 接收到的校验和

    // ── 辅助 ────────────────────────────────────────────────────
    // 计算 40 字节 payload (offset 4~43) 的字节和校验
    static uint16_t computeChecksum(const uint8_t *payload, int len);
};

// 计算校验和：简单无符号字节累加，取低 16 位
inline uint16_t MagCollectorFrame::computeChecksum(const uint8_t *data, int len)
{
    uint32_t sum = 0;
    for (int i = 0; i < len; ++i)
        sum += data[i];
    return static_cast<uint16_t>(sum & 0xFFFF);
}

#endif // MAGCOLLECTORFRAME_H
