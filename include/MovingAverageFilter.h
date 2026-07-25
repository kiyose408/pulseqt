//==============================================================================
// MovingAverageFilter — 滑动平均 / 指数加权移动平均过滤器
//
// 模式：
//   Simple — 简单滑动窗口平均，窗口内所有值等权重
//   EMA    — 指数加权移动平均 (Exponential Moving Average)
//            alpha = 2 / (windowSize + 1)，只依赖上一时刻 EMA 值
//
// 线程安全：所有状态在 ParseWorker 线程中单线程访问，无需加锁
//==============================================================================

#ifndef MOVINGAVERAGEFILTER_H
#define MOVINGAVERAGEFILTER_H

#include "IFilter.h"
#include <QVector>

class MovingAverageFilter : public IFilter
{
public:
    enum Mode { Simple, EMA };

    explicit MovingAverageFilter(int windowSize = 5, Mode mode = Simple);

    DataPoint process(const DataPoint &dp) override;
    QString    name()    const override;
    bool       isEnabled() const override { return m_enabled; }
    void       setEnabled(bool on) override { m_enabled = on; }

    int  windowSize() const { return m_windowSize; }
    Mode mode()       const { return m_mode; }

private:
    void ensureChannels(int count);

    int     m_windowSize;
    Mode    m_mode;
    bool    m_enabled = true;

    // ── Simple 模式 ──
    QVector<QVector<double>> m_history;  // [channel][window]
    QVector<int>             m_heads;    // 每通道环形写入位置
    QVector<int>             m_filled;   // 每通道已填充槽数

    // ── EMA 模式 ──
    QVector<double> m_ema;        // 每通道当前 EMA 值
    QVector<bool>   m_initFlags;  // 每通道是否已完成首帧初始化
    double          m_alpha;      // 平滑系数 = 2 / (windowSize + 1)
};

#endif // MOVINGAVERAGEFILTER_H
