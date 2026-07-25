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

/**
 * @brief 滑动平均 / EMA 指数加权移动平均过滤器
 *
 * 两种模式：
 * - Simple: 环形缓冲窗口内所有值等权重求平均
 * - EMA: alpha=2/(N+1)，对最近值更敏感，适合跟踪趋势变化的信号
 *
 * 每通道独立维护历史缓冲，自适应通道数扩容。
 */
class MovingAverageFilter : public IFilter
{
public:
    enum Mode { Simple, EMA };

    /// @brief 构造
    /// @param windowSize 窗口大小（默认 5，最小 2）
    /// @param mode 模式（Simple 或 EMA）
    explicit MovingAverageFilter(int windowSize = 5, Mode mode = Simple);

    /// @brief 过滤一个数据点，返回平滑后的值
    /// @param dp 原始数据点
    /// @return 过滤后的数据点（未选中通道原值保留）
    DataPoint process(const DataPoint &dp) override;
    QString    name()    const override;
    bool       isEnabled() const override { return m_enabled; }
    void       setEnabled(bool on) override { m_enabled = on; }

    /// @brief 返回窗口大小
    int  windowSize() const { return m_windowSize; }
    /// @brief 返回当前模式
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
