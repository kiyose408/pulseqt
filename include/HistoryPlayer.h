#ifndef HISTORYPLAYER_H
#define HISTORYPLAYER_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include "DatabaseManager.h"
#include "DataBuffer.h"

class HistoryPlayer: public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPlayer(QWidget *parent = nullptr);

    void setDbPath(const QString &path);   // 自己建独立连接
    void setDataBuffer(DataBuffer *buffer);
    void setChart(class RealTimeChart *chart);  // 回放时联动图表时间轴
    void setTimeWindow(double seconds);         // 查询范围匹配窗口

    // 从数据库中获取时间范围，设置滑块的上下限
    void loadTimeRange();
    void refreshLatest();   // 仅更新 m_timeEnd，不碰滑块
    void updateTimeLabel();
    void queryAndShow(uint64_t centerTime);

    // 回放专用 DataBuffer（与实时采集隔离）
    DataBuffer *playbackBuffer() { return &m_playbackBuffer; }

signals:
    void playbackStarted();   // 开始回放（拖动滑块/播放）
    void playbackStopped();   // 停止回放

private slots:
    void onSliderMoved(int value);     // 拖动滑块
    void onPlayPause();                // 播放/暂停
    void onPlayTick();                 // 播放定时器推进

private:

    QSlider      *m_slider;
    QLabel       *m_timeLabel;
    QPushButton  *m_playBtn;
    QComboBox    *m_speedCombo;
    QTimer       *m_playTimer;
    QTimer       *m_refreshTimer;  // 定期刷新 DB 时间范围

    DatabaseManager  m_db;      // 独立 SQLite 连接（只读），不和 ParseWorker 共享
    RealTimeChart   *m_chart = nullptr;
    DataBuffer       m_playbackBuffer{10000};  // 回放专用 buffer（独立于实时）
    DataBuffer      *m_buffer = nullptr;       // 实时 buffer 引用（setDataBuffer 设置）
    uint64_t m_timeBegin  = 0;    // 数据库中最早时间
    uint64_t m_timeEnd    = 0;    // 数据库中最晚时间
    uint64_t m_currentTime = 0;   // 滑块当前位置对应的时间
    bool     m_playing     = false;
    int      m_speed       = 1;   // 1/2/5/10
    double   m_timeWindow  = 30.0; // 查询时间窗口（秒），与图表默认一致
};

#endif // HISTORYPLAYER_H
