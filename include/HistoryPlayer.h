//==============================================================================
// HistoryPlayer — 历史数据回放
//==============================================================================
//
// 功能：QSlider 时间轴 + 播放/暂停 + 速度调节 + 独立 DataBuffer。
//
// 架构：使用独立的 DatabaseManager 连接（只读），
// 查询结果写入 m_playbackBuffer，RealTimeChart 绑定后显示。
// 与实时采集的 DataBuffer 完全隔离。
//==============================================================================

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

/// @brief 历史数据回放控件
///
/// 滑块选择时间 → 从 SQLite 查询 → 填充独立 DataBuffer → 联动 RealTimeChart。
/// 速度 1x/2x/5x/10x，播放定时器按速度倍率推进滑块。
class HistoryPlayer : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPlayer(QWidget *parent = nullptr);

    /// @brief 设置 SQLite 数据库路径（独立只读连接）
    void setDbPath(const QString &path);
    /// @brief 设置实时数据缓冲引用（用于时间范围更新）
    void setDataBuffer(DataBuffer *buffer);
    /// @brief 绑定回放图表
    void setChart(class RealTimeChart *chart);
    /// @brief 设置查询时间窗口，与图表默认一致
    void setTimeWindow(double seconds);

    /// @brief 从数据库加载时间范围，设置滑块上下限
    void loadTimeRange();
    /// @brief 刷新最新时间（不改变滑块位置）
    void refreshLatest();
    /// @brief 更新时间标签显示
    void updateTimeLabel();
    /// @brief 查询 centerTime 附近的数据并填充回放缓冲区
    /// @param centerTime 查询中心时间戳
    void queryAndShow(uint64_t centerTime);

    /// @brief 返回回放专用 DataBuffer（与实时采集隔离）
    DataBuffer *playbackBuffer() { return &m_playbackBuffer; }

signals:
    /// @brief 开始回放（拖滑块或点播放）
    void playbackStarted();
    /// @brief 停止回放
    void playbackStopped();

private slots:
    void onSliderMoved(int value);
    void onPlayPause();
    void onPlayTick();

private:
    QSlider      *m_slider;
    QLabel       *m_timeLabel;
    QPushButton  *m_playBtn;
    QComboBox    *m_speedCombo;
    QTimer       *m_playTimer;
    QTimer       *m_refreshTimer;

    DatabaseManager  m_db;                // 独立 SQLite 连接（只读）
    RealTimeChart   *m_chart = nullptr;
    DataBuffer       m_playbackBuffer{10000};
    DataBuffer      *m_buffer = nullptr;
    uint64_t m_timeBegin  = 0;
    uint64_t m_timeEnd    = 0;
    uint64_t m_currentTime = 0;
    bool     m_playing     = false;
    int      m_speed       = 1;
    double   m_timeWindow  = 30.0;
};

#endif // HISTORYPLAYER_H
