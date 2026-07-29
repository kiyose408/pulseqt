//==============================================================================
// HistoryPlayer — 历史数据回放 v2
//==============================================================================

#ifndef HISTORYPLAYER_H
#define HISTORYPLAYER_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QTimer>
#include <QPainter>
#include <QMouseEvent>
#include "DatabaseManager.h"
#include "DataBuffer.h"

/// @brief 数据段进度条（自绘 QWidget，支持无限缩放）
class SegmentBar : public QWidget
{
    Q_OBJECT
public:
    explicit SegmentBar(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(18);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setDataSlots(const QVector<int> &s) { m_slots = s; update(); }
    void setCurrentSlot(int s) { m_curSlot = s; update(); }

signals:
    void clicked(int slotIndex);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        int w = width(), h = height();
        if (w <= 0) return;
        double sw = (double)w / 96;
        for (int i = 0; i < 96; ++i) {
            QColor c;
            if (i == m_curSlot) c = QColor(0x4C, 0xB4, 0xE6);
            else if (i < m_slots.size() && m_slots[i] > 0) c = QColor(0x73, 0xC8, 0x73);
            else c = QColor(0xE0, 0xE0, 0xE0);
            p.fillRect(QRectF(i * sw, 0, sw, h), c);
        }
        p.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), 1));
        for (int hh : {0, 6, 12, 18})
            p.drawLine(QPointF(hh * sw * 4, 0), QPointF(hh * sw * 4, h));
    }
    void mousePressEvent(QMouseEvent *e) override {
        int w = width();
        if (w > 0) emit clicked(e->pos().x() * 96 / w);
    }
private:
    QVector<int> m_slots;
    int m_curSlot = 0;
};

// ═══════════════════════════════════════════════════════════

class HistoryPlayer : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPlayer(QWidget *parent = nullptr);

    void setDbPath(const QString &path);
    void setDataBuffer(DataBuffer *buffer);
    void setChart(class RealTimeChart *chart);
    void setTimeWindow(double seconds);

    void loadTimeRange();
    void refreshLatest();
    void updateTimeLabel();
    void queryAndShow(uint64_t centerTime);
    DataBuffer *playbackBuffer() { return &m_playbackBuffer; }

signals:
    void playbackStarted();
    void playbackStopped();

private slots:
    void onPlayPause();
    void onPlayTick();
    void onDateChanged(const QDate &date);
    void onLocateTime();
    void onSegmentClicked(int slotIndex);

private:
    void refreshSegmentBar();
    void loadDayDistribution(qint64 dayStart, qint64 dayEnd);

    QDateEdit    *m_dateEdit;
    QTimeEdit    *m_timeEdit;
    QPushButton  *m_locateBtn;
    SegmentBar   *m_segmentBar;
    QLabel       *m_timeLabel;
    QPushButton  *m_playBtn;
    QComboBox    *m_speedCombo;
    QTimer       *m_playTimer;
    QTimer       *m_refreshTimer;

    DatabaseManager  m_db;
    RealTimeChart   *m_chart = nullptr;
    DataBuffer       m_playbackBuffer{10000};
    DataBuffer      *m_buffer = nullptr;

    uint64_t m_timeBegin  = 0;
    uint64_t m_timeEnd    = 0;
    uint64_t m_currentTime = 0;
    bool     m_playing     = false;
    int      m_speed       = 1;
    double   m_timeWindow  = 30.0;

    static constexpr int SLOT_COUNT = 96;
    QVector<int> m_dataSlots;
    qint64 m_currentDayStart = 0;
};

#endif // HISTORYPLAYER_H
