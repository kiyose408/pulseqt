//==============================================================================
// MovingAverageFilter 实现
//==============================================================================

#include "MovingAverageFilter.h"
#include <QtMath>

MovingAverageFilter::MovingAverageFilter(int windowSize, Mode mode)
    : m_windowSize(qMax(2, windowSize))   // 最小窗口=2，否则无平滑意义
    , m_mode(mode)
    , m_alpha(2.0 / (m_windowSize + 1.0))
{
}

QString MovingAverageFilter::name() const
{
    return QString("MovingAverage(%1, %2)")
        .arg(m_windowSize)
        .arg(m_mode == EMA ? "EMA" : "Simple");
}

void MovingAverageFilter::ensureChannels(int count)
{
    int curSize = (m_mode == EMA) ? m_ema.size() : m_history.size();
    if (curSize >= count) return;

    if (m_mode == EMA) {
        // EMA 模式：只需 ema 状态 + initFlags 首帧标记
        m_ema.resize(count);
        m_initFlags.resize(count);
        return;
    }

    // Simple 模式扩容环形缓冲
    int oldSize = m_history.size();
    m_history.resize(count);
    m_heads.resize(count);
    m_filled.resize(count);

    for (int ch = oldSize; ch < count; ++ch) {
        m_history[ch].resize(m_windowSize);
        m_heads[ch]  = 0;
        m_filled[ch] = 0;
    }
}

DataPoint MovingAverageFilter::process(const DataPoint &dp)
{
    if (!m_enabled) return dp;

    ensureChannels(dp.channels.size());
    DataPoint result = dp;

    if (m_mode == EMA) {
        // ── 指数加权移动平均 ──
        // EMA = alpha * newValue + (1 - alpha) * ema_prev
        for (int ch = 0; ch < dp.channels.size(); ++ch) {
            if (!channelActive(ch)) continue;
            if (!channelActive(ch)) continue;
            double cur = dp.channels[ch];
            // 首帧直接初始化
            if (!m_initFlags[ch]) {
                m_ema[ch] = cur;
                m_initFlags[ch] = true;
            } else {
                m_ema[ch] = m_alpha * cur + (1.0 - m_alpha) * m_ema[ch];
            }
            result.channels[ch] = m_ema[ch];
        }
    } else {
        // ── 简单滑动窗口平均 ──
        for (int ch = 0; ch < dp.channels.size(); ++ch) {
            if (!channelActive(ch)) continue;
            double cur = dp.channels[ch];

            // 环形写入
            m_history[ch][m_heads[ch]] = cur;
            m_heads[ch] = (m_heads[ch] + 1) % m_windowSize;
            if (m_filled[ch] < m_windowSize) m_filled[ch]++;

            // 对已填充槽位求平均
            double sum = 0.0;
            int active = m_filled[ch];
            for (int i = 0; i < active; ++i)
                sum += m_history[ch][i];

            result.channels[ch] = sum / active;
        }
    }
    return result;
}
