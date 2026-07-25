//==============================================================================
// ThresholdAlarm — 阈值告警引擎
//
// 继承 QObject（发射信号）+ IFilter（接入 FilterPipeline 数据管道）
// 每通道独立上下限配置 + 全局滞回（Hysteresis）防抖动。
//
// 数据流：process(dp) 透明透传，仅检测阈值并发射告警/清除信号。
//
// 使用示例：
//   ThresholdAlarm alarm;
//   alarm.setUpperLimit(0, 900);   // CH0 上限 900, 滞回 10
//   alarm.setHysteresis(10);
//   connect(&alarm, &ThresholdAlarm::alarmTriggered, ...);
//==============================================================================

#ifndef THRESHOLDALARM_H
#define THRESHOLDALARM_H

#include <QObject>
#include <QVector>
#include "IFilter.h"

/**
 * @brief 阈值告警引擎（QObject + IFilter 多重继承）
 *
 * 状态机：Normal → AboveUpper/BelowLower → Normal（滞回恢复）
 * 只在状态切换瞬间发射信号，滞回带内不反复触发。
 * process() 纯透传，不修改数据。
 */
class ThresholdAlarm : public QObject, public IFilter
{
    Q_OBJECT

public:
    explicit ThresholdAlarm(QObject *parent = nullptr);

    // ── IFilter 接口 ──
    DataPoint process(const DataPoint &dp) override;
    QString    name()    const override { return "ThresholdAlarm"; }
    bool       isEnabled() const override { return m_enabled; }
    void       setEnabled(bool on) override { m_enabled = on; }

    /// @brief 设置通道上限（0=禁用）
    void setUpperLimit(int ch, double val);
    /// @brief 设置通道下限（0=禁用）
    void setLowerLimit(int ch, double val);
    double upperLimit(int ch) const;
    double lowerLimit(int ch) const;

    /// @brief 全局滞回宽度，默认 2.0
    void   setHysteresis(double h) { m_hysteresis = h; }
    double hysteresis() const { return m_hysteresis; }

signals:
    /// @brief 告警触发
    /// @param channel 通道号
    /// @param value 越限值
    /// @param threshold 阈值
    /// @param isUpper true=超上限, false=低下限
    void alarmTriggered(int channel, double value, double threshold, bool isUpper);

    /// @brief 告警清除
    /// @param channel 通道号
    void alarmCleared(int channel);

private:
    void ensureConfig(int ch);

    enum State { Normal, AboveUpper, BelowLower };

    struct ChannelConfig {
        double upper = 0;
        double lower = 0;
        bool   upperEnabled = false;
        bool   lowerEnabled = false;
    };

    QVector<ChannelConfig> m_configs;
    QVector<State>         m_states;

    double m_hysteresis = 2.0;
    bool   m_enabled    = true;
};

#endif // THRESHOLDALARM_H
