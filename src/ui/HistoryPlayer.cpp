#include "HistoryPlayer.h"
#include "RealTimeChart.h"
#include <QHBoxLayout>
#include <QDateTime>

HistoryPlayer::HistoryPlayer(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);

    m_playBtn = new QPushButton("▶", this);
    m_playBtn->setFixedWidth(30);
    connect(m_playBtn, &QPushButton::clicked, this, &HistoryPlayer::onPlayPause);

    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItems({"1x", "2x", "5x", "10x"});

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);
    connect(m_slider, &QSlider::valueChanged, this, &HistoryPlayer::onSliderMoved);

    m_timeLabel = new QLabel("00:00:00", this);
    m_timeLabel->setFixedWidth(90);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(50);
    connect(m_playTimer, &QTimer::timeout, this, &HistoryPlayer::onPlayTick);

    // 每 3 秒刷新 DB 最新时间戳（滑块范围自动扩展）
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(3000);
    connect(m_refreshTimer, &QTimer::timeout, this, &HistoryPlayer::refreshLatest);
    m_refreshTimer->start();

    layout->addWidget(m_playBtn);
    layout->addWidget(m_speedCombo);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_timeLabel);

    setEnabled(false);
}

void HistoryPlayer::updateTimeLabel()
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(m_currentTime);
    m_timeLabel->setText(dt.toString("hh:mm:ss"));
}

void HistoryPlayer::setDbPath(const QString &path)
{
    m_db.init(path);
}

void HistoryPlayer::setDataBuffer(DataBuffer *buffer)
{
    m_buffer = buffer;
}

void HistoryPlayer::setChart(RealTimeChart *chart)
{
    m_chart = chart;
}

void HistoryPlayer::setTimeWindow(double seconds)
{
    m_timeWindow = seconds;
}

void HistoryPlayer::loadTimeRange()
{
    uint64_t oldEnd = m_timeEnd;

    m_timeBegin = m_db.minTimestamp();
    m_timeEnd   = m_db.maxTimestamp();

    if (m_timeEnd <= m_timeBegin) {
        setEnabled(false);
        return;
    }

    m_slider->setRange(0, 10000);
    m_slider->setValue(10000);
    m_currentTime = m_timeEnd;
    updateTimeLabel();
    setEnabled(true);

    // 范围扩大时保持滑块在末尾
    Q_UNUSED(oldEnd);
}

void HistoryPlayer::refreshLatest()
{
    uint64_t newEnd = m_db.maxTimestamp();
    if (newEnd == 0) return;                    // DB 仍为空

    // 滑块尚未启用（DB 从空 → 有数据）→ 完整初始化
    if (!isEnabled()) {
        loadTimeRange();
        return;
    }

    if (newEnd <= m_timeEnd) return;            // 无新数据
    m_timeEnd = newEnd;
}

void HistoryPlayer::queryAndShow(uint64_t centerTime)
{
    // 查询范围 = 图表当前时间窗口（跟随缩放; 最大 120s）
    //    从图表实时读数，不依赖 setTimeWindow 一次性设置
    double   winSec    = m_chart ? m_chart->timeWindow() : m_timeWindow;
    uint64_t querySpan = static_cast<uint64_t>(winSec * 1000.0 * 1.2); // 秒→ms +20%余量
    uint64_t tBegin    = (centerTime > querySpan) ? (centerTime - querySpan) : 0;
    uint64_t tEnd      = centerTime;  // 滑块 = 最右侧，不查未来

    auto results = m_db.query(tBegin, tEnd);
    m_playbackBuffer.clear();
    for (auto &dp : results)
        m_playbackBuffer.push(dp);
}

void HistoryPlayer::onSliderMoved(int value)
{
    if (!isEnabled()) return;   // DB 无数据 → 滑块未启用
    refreshLatest();    // 拖动时刷新最新时间范围

    double ratio = value / 10000.0;
    m_currentTime = m_timeBegin + static_cast<uint64_t>((m_timeEnd - m_timeBegin) * ratio);
    updateTimeLabel();
    if (m_chart) m_chart->setCurrentTime(static_cast<qint64>(m_currentTime));
    queryAndShow(m_currentTime);
}

void HistoryPlayer::onPlayPause()
{
    m_playing = !m_playing;

    if (m_playing) {
        m_playBtn->setText("⏸");
        m_playTimer->start();
    } else {
        m_playBtn->setText("▶");
        m_playTimer->stop();
        if (m_chart) m_chart->setCurrentTime(0);
    }
}

void HistoryPlayer::onPlayTick()
{
    // index 0→1x, 1→2x, 2→5x, 3→10x
    int multipliers[] = {1, 2, 5, 10};
    int speed = multipliers[m_speedCombo->currentIndex()];

    // 按真实时间推进：每次 +50ms × 速度
    m_currentTime += 50ULL * speed;

    if (m_currentTime >= m_timeEnd) {
        m_currentTime = m_timeEnd;
        onPlayPause();
    }

    // 时间 → 滑块位置（blockSignals 防止递归触发 onSliderMoved）
    double ratio = double(m_currentTime - m_timeBegin) / (m_timeEnd - m_timeBegin);
    m_slider->blockSignals(true);
    m_slider->setValue(static_cast<int>(ratio * 10000));
    m_slider->blockSignals(false);

    updateTimeLabel();
    if (m_chart) m_chart->setCurrentTime(static_cast<qint64>(m_currentTime));
    queryAndShow(m_currentTime);
}
