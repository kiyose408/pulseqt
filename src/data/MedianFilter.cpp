//==============================================================================
// MedianFilter 实现
//==============================================================================

#include "MedianFilter.h"
#include <algorithm>
#include <QtMath>

MedianFilter::MedianFilter(int windowSize)
    : m_windowSize(qMax(3, windowSize))   // 最小窗口=3，否则中位数无意义
{
}

QString MedianFilter::name() const
{
    return QString("Median(%1)").arg(m_windowSize);
}

DataPoint MedianFilter::process(const DataPoint &dp)
{
    if (!m_enabled) return dp;

    DataPoint result = dp;
    int channels = dp.channels.size();

    if (m_history.size() < channels)
        m_history.resize(channels);

    for (int ch = 0; ch < channels; ++ch) {
        if (!channelActive(ch)) continue;

        auto &win = m_history[ch];

        // 追加新值，保持窗口大小
        win.append(dp.channels[ch]);
        if (win.size() > m_windowSize)
            win.removeFirst();

        // 拷贝 → nth_element 取中位数
        QVector<double> sorted = win;
        int mid = sorted.size() / 2;
        std::nth_element(sorted.begin(), sorted.begin() + mid, sorted.end());
        result.channels[ch] = sorted[mid];
    }
    return result;
}
