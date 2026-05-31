//==============================================================================
// ProtocolDecoder - 二进制协议解码器（状态机）
//
// 将 Channel 层传来的原始字节流逐字节解析为完整 Frame。
// 核心：7 状态状态机，天然支持粘包/拆包处理。
//
// 状态流转：
//   WAIT_HEADER_H → WAIT_HEADER_L → WAIT_LENGTH → WAIT_TYPE
//       → WAIT_PAYLOAD → WAIT_CRC_L → WAIT_CRC_H → 回到 WAIT_HEADER_H
//
// 关键设计：
//   1. feed() 逐字节喂入，不缓存输入（状态机靠 m_state 跨调用保持位置）
//   2. Payload 阶段按长度读取，不扫描内容 → 帧内含 0xA55A 不误判
//   3. CRC 校验失败不 emit frameDecoded，记录日志并丢弃
//   4. reset() 可随时重置状态机（通道切换时使用）
//==============================================================================

#ifndef PROTOCOLDECODER_H
#define PROTOCOLDECODER_H

#include <QObject>
#include <QByteArray>
#include <cstdint>
#include "Frame.h"

class ProtocolDecoder : public QObject
{
    Q_OBJECT

public:
    // 喂入原始字节数据（可多次调用，状态机跨调用保持进度）
    void feed(const QByteArray &data);

    // 重置状态机到初始状态（通道切换、协议错乱恢复时使用）
    void reset();

signals:
    // CRC 校验通过的完整帧
    void frameDecoded(const Frame &frame);

    // CRC 校验失败的原始数据（供调试）
    void crcError(const QByteArray &rawData);

private:
    // ── 7 状态解码状态机 ───────────────────────────────────────────
    enum State {
        WAIT_HEADER_H,   // 等待帧头第 1 字节 0xA5
        WAIT_HEADER_L,   // 等待帧头第 2 字节 0x5A
        WAIT_LENGTH,     // 读取 Payload 长度（1 字节，0~255）
        WAIT_TYPE,       // 读取帧类型
        WAIT_PAYLOAD,    // 按长度逐字节读取 Payload
        WAIT_CRC_L,      // 读取 CRC16 低字节
        WAIT_CRC_H       // 读取 CRC16 高字节 → 校验 → emit / 丢弃
    };

    State m_state = WAIT_HEADER_H;   // 当前状态（跨 feed() 调用保持）

    // ── 帧数据累积 ─────────────────────────────────────────────────
    QByteArray m_buffer;             // 当前帧 Header~Payload（用于 CRC 计算）
    QByteArray m_payload;            // 当前帧 Payload（不含 Header/Length/Type/CRC）
    uint16_t   m_crcReceived = 0;    // 收到的 CRC16 值（小端序拼合）
    uint8_t    m_payloadLen   = 0;   // Payload 字节数
    uint8_t    m_frameType    = 0;   // 当前帧类型
    int        m_payloadIdx   = 0;   // Payload 已读字节索引
};

#endif // PROTOCOLDECODER_H
