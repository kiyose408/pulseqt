//==============================================================================
// AlarmPanel — 告警面板（抗压版）
//
// 限流：同通道同方向 1s 内去重，防止噪声洪流淹死 UI
//==============================================================================

#ifndef ALARMPANEL_H
#define ALARMPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <QPair>

class DatabaseManager;

class AlarmPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AlarmPanel(DatabaseManager *db, QWidget *parent = nullptr);
    void setDatabase(DatabaseManager *db) { m_db = db; }

public slots:
    void onAlarmTriggered(int channel, double value, double threshold, bool isUpper);
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
    DatabaseManager  *m_db;
    int               m_totalTriggered = 0;

    // 限流：key=(channel, isUpper) → 上次触发时间
    QMap<QPair<int,bool>, qint64> m_lastTrigger;
    QMap<QPair<int,bool>, double> m_pendingDedup;
};

#endif // ALARMPANEL_H
