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

    // ── 阈值配置（0 = 禁用该告警） ──
    void setUpperLimit(int ch, double val);
    void setLowerLimit(int ch, double val);
    double upperLimit(int ch) const;
    double lowerLimit(int ch) const;

    // ── 滞回 ──
    void   setHysteresis(double h) { m_hysteresis = h; }
    double hysteresis() const { return m_hysteresis; }

signals:
    // 告警触发：channel=通道, value=越限值, threshold=阈值, isUpper=超上限/低下限
    void alarmTriggered(int channel, double value, double threshold, bool isUpper);

    // 告警清除
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

    double m_hysteresis = 2.0;   // 全局滞回带宽度
    bool   m_enabled    = true;
};

#endif // THRESHOLDALARM_H
