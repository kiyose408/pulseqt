//==============================================================================
// ThresholdAlarm 实现
//==============================================================================

#include "ThresholdAlarm.h"
#include <QDebug>

ThresholdAlarm::ThresholdAlarm(QObject *parent)
    : QObject(parent)
{
}

void ThresholdAlarm::ensureConfig(int ch)
{
    if (ch < m_configs.size()) return;
    m_configs.resize(ch + 1);
    m_states.resize(ch + 1);
}

void ThresholdAlarm::setUpperLimit(int ch, double val)
{
    ensureConfig(ch);
    m_configs[ch].upper = val;
    m_configs[ch].upperEnabled = (val != 0);
}

void ThresholdAlarm::setLowerLimit(int ch, double val)
{
    ensureConfig(ch);
    m_configs[ch].lower = val;
    m_configs[ch].lowerEnabled = (val != 0);
}

double ThresholdAlarm::upperLimit(int ch) const
{
    return (ch < m_configs.size()) ? m_configs[ch].upper : 0;
}

double ThresholdAlarm::lowerLimit(int ch) const
{
    return (ch < m_configs.size()) ? m_configs[ch].lower : 0;
}

DataPoint ThresholdAlarm::process(const DataPoint &dp)
{
    if (!m_enabled) return dp;

    for (int ch = 0; ch < dp.channels.size(); ++ch) {
        if (!channelActive(ch)) continue;

        ensureConfig(ch);
        double val = dp.channels[ch];
        const auto &cfg = m_configs[ch];
        auto &state = m_states[ch];

        // 越上限检测
        if (cfg.upperEnabled && val >= cfg.upper) {
            if (state != AboveUpper) {
                state = AboveUpper;
                emit alarmTriggered(ch, val, cfg.upper, true);
                qInfo() << "ThresholdAlarm: CH" << ch << "超上限"
                        << val << ">=" << cfg.upper;
            }
        }
        // 越下限检测
        else if (cfg.lowerEnabled && val <= cfg.lower) {
            if (state != BelowLower) {
                state = BelowLower;
                emit alarmTriggered(ch, val, cfg.lower, false);
                qInfo() << "ThresholdAlarm: CH" << ch << "低下限"
                        << val << "<=" << cfg.lower;
            }
        }
        // 滞回恢复检测
        else if (state == AboveUpper && val < (cfg.upper - m_hysteresis)) {
            state = Normal;
            emit alarmCleared(ch);
            qInfo() << "ThresholdAlarm: CH" << ch << "上限告警清除";
        }
        else if (state == BelowLower && val > (cfg.lower + m_hysteresis)) {
            state = Normal;
            emit alarmCleared(ch);
            qInfo() << "ThresholdAlarm: CH" << ch << "下限告警清除";
        }
    }
    return dp;  // 纯透传
}
