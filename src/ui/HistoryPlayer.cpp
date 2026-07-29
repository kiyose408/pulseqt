//==============================================================================
// HistoryPlayer v2 — 日期定位 + 数据段进度条
//==============================================================================

#include "HistoryPlayer.h"
#include "RealTimeChart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSqlQuery>
#include <QDateTime>
#include <QDebug>

HistoryPlayer::HistoryPlayer(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);

    // ── 第一行：播放控制 + 日期时间 + 定位 ──
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(4);

    m_playBtn = new QPushButton("▶", this);
    m_playBtn->setFixedWidth(30);
    connect(m_playBtn, &QPushButton::clicked, this, &HistoryPlayer::onPlayPause);

    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItems({"1x", "2x", "5x", "10x"});

    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");
    connect(m_dateEdit, &QDateEdit::dateChanged, this, &HistoryPlayer::onDateChanged);

    m_timeEdit = new QTimeEdit(QTime::currentTime(), this);
    m_timeEdit->setDisplayFormat("HH:mm:ss");

    m_locateBtn = new QPushButton("定位", this);
    m_locateBtn->setFixedWidth(40);
    connect(m_locateBtn, &QPushButton::clicked, this, &HistoryPlayer::onLocateTime);

    m_timeLabel = new QLabel("--:--:--", this);
    m_timeLabel->setFixedWidth(70);

    row1->addWidget(m_playBtn);
    row1->addWidget(m_speedCombo);
    row1->addWidget(m_dateEdit);
    row1->addWidget(m_timeEdit);
    row1->addWidget(m_locateBtn);
    row1->addWidget(m_timeLabel);
    row1->addStretch();
    mainLayout->addLayout(row1);

    // ── 第二行：数据段进度条（自绘 SegmentBar） ──
    m_segmentBar = new SegmentBar(this);
    connect(m_segmentBar, &SegmentBar::clicked, this, &HistoryPlayer::onSegmentClicked);
    mainLayout->addWidget(m_segmentBar);

    // ── 定时器 ──
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(50);
    connect(m_playTimer, &QTimer::timeout, this, &HistoryPlayer::onPlayTick);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(3000);
    connect(m_refreshTimer, &QTimer::timeout, this, &HistoryPlayer::refreshLatest);
    m_refreshTimer->start();

    m_dataSlots.resize(SLOT_COUNT);
    setEnabled(false);
}

void HistoryPlayer::updateTimeLabel()
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(m_currentTime);
    m_timeLabel->setText(dt.toString("hh:mm:ss"));
}

void HistoryPlayer::setDbPath(const QString &path)  { m_db.init(path); }
void HistoryPlayer::setDataBuffer(DataBuffer *b)    { m_buffer = b; }
void HistoryPlayer::setChart(RealTimeChart *c)       { m_chart = c; }
void HistoryPlayer::setTimeWindow(double s)           { m_timeWindow = s; }

void HistoryPlayer::loadTimeRange()
{
    m_timeBegin = m_db.minTimestamp();
    m_timeEnd   = m_db.maxTimestamp();
    qInfo() << "HistoryPlayer: loadTimeRange begin=" << m_timeBegin << "end=" << m_timeEnd;

    if (m_timeEnd <= m_timeBegin) {
        setEnabled(false);
        return;
    }
    m_currentTime = m_timeEnd;
    updateTimeLabel();
    setEnabled(true);

    QDateTime endDt = QDateTime::fromMSecsSinceEpoch(m_timeEnd);
    m_dateEdit->setDate(endDt.date());
    onDateChanged(endDt.date());
}

void HistoryPlayer::refreshLatest()
{
    uint64_t newEnd = m_db.maxTimestamp();
    if (newEnd == 0) return;
    if (!isEnabled()) {
        qInfo() << "HistoryPlayer: refreshLatest detected data, reloading";
        loadTimeRange();
        return;
    }
    if (newEnd <= m_timeEnd) return;
    m_timeEnd = newEnd;
}

void HistoryPlayer::onDateChanged(const QDate &date)
{
    QDateTime dayStart(date, QTime(0, 0, 0));
    QDateTime dayEnd(date, QTime(23, 59, 59, 999));
    m_currentDayStart = dayStart.toMSecsSinceEpoch();
    loadDayDistribution(m_currentDayStart, dayEnd.toMSecsSinceEpoch());
    refreshSegmentBar();
}

void HistoryPlayer::loadDayDistribution(qint64 dayStart, qint64 dayEnd)
{
    m_dataSlots.fill(0);
    if (dayStart <= 0) return;

    qint64 slotMs = (dayEnd - dayStart) / SLOT_COUNT;
    if (slotMs <= 0) return;

    QSqlQuery query(m_db.dbHandle());
    query.prepare(QString(
        "SELECT (timestamp - %1) / %2 AS slot, COUNT(*) "
        "FROM data_points "
        "WHERE timestamp BETWEEN %1 AND %3 "
        "GROUP BY slot").arg(dayStart).arg(slotMs).arg(dayEnd));
    query.exec();

    while (query.next()) {
        int slot = query.value(0).toInt();
        int cnt  = query.value(1).toInt();
        if (slot >= 0 && slot < SLOT_COUNT && cnt > 0)
            m_dataSlots[slot] = cnt;
    }
}

void HistoryPlayer::refreshSegmentBar()
{
    qint64 dayLen = 24LL * 3600 * 1000;
    int curSlot = (m_currentTime - m_currentDayStart) * SLOT_COUNT / dayLen;
    curSlot = qBound(0, curSlot, SLOT_COUNT - 1);
    m_segmentBar->setDataSlots(m_dataSlots);
    m_segmentBar->setCurrentSlot(curSlot);
}

void HistoryPlayer::onSegmentClicked(int slotIndex)
{
    if (!isEnabled()) return;
    qint64 slotMs = 24LL * 3600 * 1000 / SLOT_COUNT;
    m_currentTime = m_currentDayStart + slotIndex * slotMs;
    updateTimeLabel();
    if (m_chart) m_chart->setCurrentTime(static_cast<qint64>(m_currentTime));
    queryAndShow(m_currentTime);
    refreshSegmentBar();
}

void HistoryPlayer::onLocateTime()
{
    if (!isEnabled()) return;
    QDateTime dt(m_dateEdit->date(), m_timeEdit->time());
    m_currentTime = dt.toMSecsSinceEpoch();
    updateTimeLabel();
    if (m_chart) m_chart->setCurrentTime(static_cast<qint64>(m_currentTime));
    queryAndShow(m_currentTime);
    refreshSegmentBar();
}

void HistoryPlayer::queryAndShow(uint64_t centerTime)
{
    double   winSec    = m_chart ? m_chart->timeWindow() : m_timeWindow;
    uint64_t querySpan = static_cast<uint64_t>(winSec * 1000.0 * 1.2);
    uint64_t tBegin    = (centerTime > querySpan) ? (centerTime - querySpan) : 0;
    uint64_t tEnd      = centerTime;

    auto results = m_db.query(tBegin, tEnd);
    m_playbackBuffer.clear();
    for (auto &dp : results)
        m_playbackBuffer.push(dp);
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
    int multipliers[] = {1, 2, 5, 10};
    int speed = multipliers[m_speedCombo->currentIndex()];
    m_currentTime += 50ULL * speed;
    if (m_currentTime >= m_timeEnd) {
        m_currentTime = m_timeEnd;
        onPlayPause();
    }
    updateTimeLabel();
    if (m_chart) m_chart->setCurrentTime(static_cast<qint64>(m_currentTime));
    queryAndShow(m_currentTime);
    refreshSegmentBar();
}
