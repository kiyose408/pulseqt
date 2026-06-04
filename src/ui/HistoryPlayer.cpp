#include "HistoryPlayer.h"
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

void HistoryPlayer::loadTimeRange()
{
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
}

void HistoryPlayer::queryAndShow(uint64_t centerTime)
{
    if (!m_buffer) return;

    uint64_t tBegin = (centerTime > 15000) ? (centerTime - 15000) : 0;
    uint64_t tEnd   = centerTime + 15000;

    auto results = m_db.query(tBegin, tEnd);
    m_buffer->clear();
    for (auto &dp : results)
        m_buffer->push(dp);
}

void HistoryPlayer::onSliderMoved(int value)
{
    if (!m_buffer) return;

    double ratio = value / 10000.0;
    m_currentTime = m_timeBegin + static_cast<uint64_t>((m_timeEnd - m_timeBegin) * ratio);
    updateTimeLabel();
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
    queryAndShow(m_currentTime);
}
