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

class MedianFilter : public IFilter
{
public:
    explicit MedianFilter(int windowSize = 5);

    DataPoint process(const DataPoint &dp) override;
    QString    name()    const override;
    bool       isEnabled() const override { return m_enabled; }
    void       setEnabled(bool on) override { m_enabled = on; }

    int windowSize() const { return m_windowSize; }

private:
    int  m_windowSize;
    bool m_enabled = true;

    // [channel] → 最近 N 个值的有序历史
    QVector<QVector<double>> m_history;
};

#endif // MEDIANFILTER_H
