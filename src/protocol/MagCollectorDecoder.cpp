//==============================================================================
// MagCollectorDecoder 实现 — 12 状态状态机
//==============================================================================

#include "MagCollectorDecoder.h"
#include "Logger.h"
#include <QDebug>

MagCollectorDecoder::MagCollectorDecoder(QObject *parent)
    : QObject(parent)
{
}

void MagCollectorDecoder::reset()
{
    m_state       = WAIT_HEADER_H;
    m_payloadBuf.clear();
    m_byteIdx     = 0;
    m_frameLength = 0;
    m_sequence    = 0;
    m_checksumRx  = 0;
}

void MagCollectorDecoder::feed(const QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i) {
        processByte(static_cast<uint8_t>(data[i]));
    }
}

//==============================================================================
// int24 解码：3 字节小端序 → int32（符号扩展）
//==============================================================================
int32_t MagCollectorDecoder::decodeInt24(const uint8_t *bytes)
{
    // 拼接 24-bit 无符号值
    uint32_t raw = static_cast<uint32_t>(bytes[0])
                 | (static_cast<uint32_t>(bytes[1]) << 8)
                 | (static_cast<uint32_t>(bytes[2]) << 16);

    // 符号扩展：bit23 = 1 → 负数
    if (raw & 0x800000u) {
        return static_cast<int32_t>(raw | 0xFF000000u);
    }
    return static_cast<int32_t>(raw);
}

//==============================================================================
// 逐字节状态机
//==============================================================================
void MagCollectorDecoder::processByte(uint8_t byte)
{
    switch (m_state) {

    // ── WAIT_HEADER_H：找 'F' ──────────────────────────────
    case WAIT_HEADER_H:
        if (byte == MagCollectorFrame::HEADER_H) {
            m_state = WAIT_HEADER_L;
        }
        // 不是 'F' → 继续在本状态等待
        break;

    // ── WAIT_HEADER_L：必须是 'M' ──────────────────────────
    case WAIT_HEADER_L:
        if (byte == MagCollectorFrame::HEADER_L) {
            // 帧头匹配成功，初始化 payload 缓冲
            m_payloadBuf.clear();
            m_state = WAIT_LENGTH_L;
        } else {
            // 不是 'M'，利用当前字节重新检查是否是另一个帧头
            m_state = WAIT_HEADER_H;
            // 当前字节恰好是 'F' → 转到下一状态
            if (byte == MagCollectorFrame::HEADER_H) {
                m_state = WAIT_HEADER_L;
            }
        }
        break;

    // ── WAIT_LENGTH_L ─────────────────────────────────────
    case WAIT_LENGTH_L:
        m_frameLength = byte;        // 低字节
        m_state = WAIT_LENGTH_H;
        break;

    // ── WAIT_LENGTH_H ─────────────────────────────────────
    case WAIT_LENGTH_H:
        m_frameLength |= (static_cast<uint16_t>(byte) << 8);  // 高字节

        // 长度合法性检查
        if (m_frameLength != MagCollectorFrame::RAW_FRAME_SIZE) {
            qWarning() << "MagCollectorDecoder: unexpected frame length"
                       << m_frameLength << "(expected" << MagCollectorFrame::RAW_FRAME_SIZE << ")";
            // 不直接丢弃，继续解码（兼容未来可能的帧长变更）
            // 但 payload 缓冲区大小按标准 40 字节来
        }
        m_byteIdx = 0;
        m_state = WAIT_SEQUENCE;
        break;

    // ── WAIT_SEQUENCE：读 4 字节 ───────────────────────────
    case WAIT_SEQUENCE:
        m_payloadBuf.append(static_cast<char>(byte));
        if (++m_byteIdx >= 4) {
            // 解析序列号（小端序）
            auto *p = reinterpret_cast<const uint8_t*>(m_payloadBuf.constData());
            m_sequence = static_cast<uint32_t>(p[0])
                       | (static_cast<uint32_t>(p[1]) << 8)
                       | (static_cast<uint32_t>(p[2]) << 16)
                       | (static_cast<uint32_t>(p[3]) << 24);
            m_byteIdx = 0;
            m_state = WAIT_TIMESTAMP;
        }
        break;

    // ── WAIT_TIMESTAMP：读 12 字节 ─────────────────────────
    case WAIT_TIMESTAMP:
        m_payloadBuf.append(static_cast<char>(byte));
        if (++m_byteIdx >= 12) {
            // 拷贝时间信息
            auto *p = reinterpret_cast<const uint8_t*>(
                m_payloadBuf.constData() + m_payloadBuf.size() - 12);
            for (int i = 0; i < 12; ++i)
                m_timestamp[i] = p[i];
            m_byteIdx = 0;
            m_state = WAIT_CHANNELS;
        }
        break;

    // ── WAIT_CHANNELS：读 24 字节（6×(3B data + 1B flag)）──
    case WAIT_CHANNELS:
        m_payloadBuf.append(static_cast<char>(byte));
        if (++m_byteIdx >= 24) {
            auto *p = reinterpret_cast<const uint8_t*>(
                m_payloadBuf.constData() + m_payloadBuf.size() - 24);

            for (int ch = 0; ch < 6; ++ch) {
                int offset = ch * 4;  // 每通道 4 字节: 3B data + 1B flag
                m_data[ch]  = decodeInt24(p + offset);
                m_flags[ch] = p[offset + 3];
            }

            m_byteIdx = 0;
            m_state = WAIT_CHECKSUM_L;
        }
        break;

    // ── WAIT_CHECKSUM_L ────────────────────────────────────
    case WAIT_CHECKSUM_L:
        m_checksumRx = byte;         // 低字节
        m_state = WAIT_CHECKSUM_H;
        break;

    // ── WAIT_CHECKSUM_H ────────────────────────────────────
    case WAIT_CHECKSUM_H:
        m_checksumRx |= (static_cast<uint16_t>(byte) << 8);
        m_state = WAIT_FOOTER_L;
        break;

    // ── WAIT_FOOTER_L：必须是 0x0D ─────────────────────────
    case WAIT_FOOTER_L:
        if (byte == MagCollectorFrame::FOOTER_H) {
            m_state = WAIT_FOOTER_H;
        } else {
            // 帧尾不匹配 → 丢弃，尝试在当前字节重新同步
            qInfo() << "MagCollectorDecoder: footer CR mismatch, resync";
            m_state = WAIT_HEADER_H;
            if (byte == MagCollectorFrame::HEADER_H) {
                m_state = WAIT_HEADER_L;
            }
        }
        break;

    // ── WAIT_FOOTER_H：必须是 0x0A → 校验 → 输出 ──────────
    case WAIT_FOOTER_H:
        if (byte == MagCollectorFrame::FOOTER_L) {
            // ── 帧尾匹配 → 计算校验和 ──────────────────────
            auto *payload = reinterpret_cast<const uint8_t*>(m_payloadBuf.constData());
            uint16_t computed = MagCollectorFrame::computeChecksum(payload,
                                                                    m_payloadBuf.size());

            if (computed != m_checksumRx) {
                qWarning() << "MagCollectorDecoder: checksum mismatch"
                           << "(computed:" << Qt::hex << computed
                           << "received:" << m_checksumRx << Qt::dec << ")";
                emit checksumError("checksum mismatch");
                m_state = WAIT_HEADER_H;
                break;
            }

            // ── 通道标志验证（可选但推荐） ──────────────────
            static const uint8_t expectedFlags[6] = {
                MagCollectorFrame::CH_FLAG_X1,
                MagCollectorFrame::CH_FLAG_Y1,
                MagCollectorFrame::CH_FLAG_Z1,
                MagCollectorFrame::CH_FLAG_X2,
                MagCollectorFrame::CH_FLAG_Y2,
                MagCollectorFrame::CH_FLAG_Z2
            };
            bool flagsOk = true;
            for (int i = 0; i < 6; ++i) {
                if (m_flags[i] != expectedFlags[i]) {
                    qWarning() << "MagCollectorDecoder: channel flag mismatch at ch"
                               << i << "expected" << Qt::hex
                               << static_cast<int>(expectedFlags[i])
                               << "got" << static_cast<int>(m_flags[i]) << Qt::dec;
                    flagsOk = false;
                    break;
                }
            }
            if (!flagsOk) {
                emit checksumError("channel flag mismatch");
                m_state = WAIT_HEADER_H;
                break;
            }

            // ── 所有校验通过 → 构建帧 ──────────────────────
            MagCollectorFrame frame;
            frame.frameLength = m_frameLength;
            frame.sequence    = m_sequence;
            for (int i = 0; i < 12; ++i) frame.timestamp[i] = m_timestamp[i];
            for (int i = 0; i < 6;  ++i) { frame.data[i] = m_data[i]; frame.flags[i] = m_flags[i]; }
            frame.checksum    = m_checksumRx;

            emit frameDecoded(frame);
        } else {
            // 帧尾第 2 字节不是 0x0A
            qInfo() << "MagCollectorDecoder: footer LF mismatch, resync";
            if (byte == MagCollectorFrame::HEADER_H) {
                m_state = WAIT_HEADER_L;
            } else {
                m_state = WAIT_HEADER_H;
            }
            break;
        }

        // 解码完成，回到初始状态
        m_state = WAIT_HEADER_H;
        break;
    }
}
