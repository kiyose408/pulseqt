//==============================================================================
// AlarmPanel — 告警面板（抗压版）
//
// 订阅 ThresholdAlarm 信号，实时显示告警列表 + 红色闪烁指示。
//
// 限流：同通道同方向 1s 内去重，防止噪声洪流淹死 UI。
// 告警条目：红色=触发，绿色=清除，最新在顶部。
//==============================================================================

#ifndef ALARMPANEL_H
#define ALARMPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <QPair>


/**
 * @brief 告警面板
 *
 * 连接 ThresholdAlarm::alarmTriggered/Cleared 信号。
 * 列表上限 100 条，超出自动丢弃旧条目。
 * 红色闪烁指示器 + 告警计数标签。
 */
class AlarmPanel : public QWidget
{
    Q_OBJECT

public:
    /// @param db DatabaseManager 指针（用于写入 alarms 表）
    explicit AlarmPanel(QWidget *parent = nullptr);

public slots:
    /// @brief 告警触发槽
    /// @param channel 通道号
    /// @param value 越限值
    /// @param threshold 阈值
    /// @param isUpper true=超上限
    void onAlarmTriggered(int channel, double value, double threshold, bool isUpper);
    /// @brief 告警清除槽
    void onAlarmCleared(int channel);

private slots:
    void blinkIndicator();

private:
    void addEntry(const QString &text, const QColor &bg);

    QListWidget      *m_list;
    QLabel           *m_indicator;
    QLabel           *m_countLabel;
    QTimer           *m_blinkTimer;
    bool              m_blinkOn = true;
    int               m_totalTriggered = 0;

    // 限流：key=(channel, isUpper) → 上次触发时间
    QMap<QPair<int,bool>, qint64> m_lastTrigger;
    QMap<QPair<int,bool>, double> m_pendingDedup;
};

#endif // ALARMPANEL_H
