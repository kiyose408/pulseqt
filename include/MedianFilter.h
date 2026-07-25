//==============================================================================
// MedianFilter — 中值滤波器
//
// 每通道维护一个滑动窗口，对窗口内值取中位数输出。
// 核心特性：对脉冲式野值（单帧异常跳变）天然免疫。
//
// 算法：std::nth_element O(n)，windowSize=5 时开销可忽略。
//
// 线程安全：所有状态在 ParseWorker 线程中单线程访问，无需加锁
//==============================================================================

#ifndef MEDIANFILTER_H
#define MEDIANFILTER_H

#include "IFilter.h"
#include <QVector>

/**
 * @brief 中值滤波器，剔除脉冲式野值
 *
 * 每通道维护最近 N 个值的滑动窗口，取中位数输出。
 * 窗口 [10, 10, 10, 10, 999] → 中位数 10，野值 999 被丢弃。
 */
class MedianFilter : public IFilter
{
public:
    /// @brief 构造
    /// @param windowSize 窗口大小（默认 5，最小 3）
    explicit MedianFilter(int windowSize = 5);

    /// @brief 过滤一个数据点
    /// @param dp 原始数据点
    /// @return 过滤后的数据点
    DataPoint process(const DataPoint &dp) override;
    QString    name()    const override;
    bool       isEnabled() const override { return m_enabled; }
    void       setEnabled(bool on) override { m_enabled = on; }

    int windowSize() const { return m_windowSize; }

private:
    int  m_windowSize;
    bool m_enabled = true;
    QVector<QVector<double>> m_history;  // [channel] → 最近 N 个值
};

#endif // MEDIANFILTER_H
