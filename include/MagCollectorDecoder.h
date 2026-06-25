//==============================================================================
// MagCollectorDecoder — 六通道磁场集采器协议解码器（状态机）
//
// 将 TCP 通道传来的原始字节流解析为完整的 MagCollectorFrame。
// 采用 12 状态状态机，天然支持粘包/拆包处理。
//
// 状态流转：
//   WAIT_HEADER_H → WAIT_HEADER_L → WAIT_LENGTH_L → WAIT_LENGTH_H
//     → WAIT_SEQUENCE (4B) → WAIT_TIMESTAMP (12B)
//     → WAIT_CHANNELS (24B: 6×(3B data + 1B flag))
//     → WAIT_CHECKSUM_L → WAIT_CHECKSUM_H
//     → WAIT_FOOTER_L → WAIT_FOOTER_H → 校验 → emit / 丢弃
//
// 关键设计：
//   1. feed() 逐字节喂入，状态跨调用保持
//   2. 通道数据阶段按固定字节数读取，不扫描内容
//   3. 校验失败不 emit frameDecoded，记录日志并丢弃
//   4. 帧尾匹配失败时在当前缓冲区中重新扫描帧头 "FM"
//   5. reset() 可随时重置状态机（通道切换时使用）
//==============================================================================

#ifndef MAGCOLLECTORDECODER_H
#define MAGCOLLECTORDECODER_H

#include <QObject>
#include <QByteArray>
#include <cstdint>
#include "MagCollectorFrame.h"

class MagCollectorDecoder : public QObject
{
    Q_OBJECT

public:
    explicit MagCollectorDecoder(QObject *parent = nullptr);

    // 喂入原始字节数据（可多次调用，状态机跨调用保持进度）
    void feed(const QByteArray &data);

    // 重置状态机到初始状态
    void reset();

signals:
    // 校验通过的完整帧
    void frameDecoded(const MagCollectorFrame &frame);

    // 校验失败的原始数据（供调试）
    void checksumError(const QString &reason);

private:
    // 处理单个字节
    void processByte(uint8_t byte);

    // 将 int24（3 字节小端序有符号）解码为 int32
    static int32_t decodeInt24(const uint8_t *bytes);

    // ── 12 状态解码状态机 ─────────────────────────────────────
    enum State {
        WAIT_HEADER_H,    // 等待帧头第 1 字节 0x46 'F'
        WAIT_HEADER_L,    // 等待帧头第 2 字节 0x4D 'M'
        WAIT_LENGTH_L,    // 等待帧长度低字节
        WAIT_LENGTH_H,    // 等待帧长度高字节
        WAIT_SEQUENCE,    // 等待 4 字节序列号
        WAIT_TIMESTAMP,   // 等待 12 字节时间信息
        WAIT_CHANNELS,    // 等待 24 字节通道数据（6×(3B数据+1B标志)）
        WAIT_CHECKSUM_L,  // 等待校验和低字节
        WAIT_CHECKSUM_H,  // 等待校验和高字节
        WAIT_FOOTER_L,    // 等待帧尾第 1 字节 0x0D
        WAIT_FOOTER_H     // 等待帧尾第 2 字节 0x0A → 校验 → emit / 丢弃
    };

    State m_state = WAIT_HEADER_H;

    // ── 帧数据累积缓冲区 ──────────────────────────────────────
    QByteArray m_payloadBuf;   // 校验覆盖区 (offset 4~43, 40 字节)
    int        m_byteIdx = 0;  // 当前状态已读字节索引

    // ── 中间解码字段 ──────────────────────────────────────────
    uint16_t  m_frameLength = 0;
    uint32_t  m_sequence    = 0;
    uint8_t   m_timestamp[12] = {};
    int32_t   m_data[6]     = {};
    uint8_t   m_flags[6]    = {};
    uint16_t  m_checksumRx  = 0;
};

#endif // MAGCOLLECTORDECODER_H
