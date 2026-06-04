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

    //从数据库中获取时间范围，设置滑块的上下限
    void loadTimeRange();
    void updateTimeLabel();
    void queryAndShow(uint64_t centerTime);
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

    DatabaseManager  m_db;      // 独立 SQLite 连接（只读），不和 ParseWorker 共享
    DataBuffer      *m_buffer = nullptr;
    uint64_t m_timeBegin  = 0;    // 数据库中最早时间
    uint64_t m_timeEnd    = 0;    // 数据库中最晚时间
    uint64_t m_currentTime = 0;   // 滑块当前位置对应的时间
    bool     m_playing     = false;
    int      m_speed       = 1;   // 1/2/5/10
};

#endif // HISTORYPLAYER_H
