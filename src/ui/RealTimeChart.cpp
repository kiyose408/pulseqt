#include "RealTimeChart.h"
#include <algorithm>
#include <QDateTime>

const QColor RealTimeChart::CH_COLORS[8] = {
    QColor(0xE6, 0x69, 0x4C),  // 橙
    QColor(0x4C, 0xB4, 0xE6),  // 蓝
    QColor(0x4C, 0xE6, 0x7A),  // 绿
    QColor(0xE6, 0x4C, 0xB4),  // 粉
    QColor(0xE6, 0xD8, 0x4C),  // 黄
    QColor(0x8E, 0x4C, 0xE6),  // 紫
    QColor(0x4C, 0xE6, 0xD8),  // 青
    QColor(0xE6, 0x4C, 0x4C),  // 红
};
// 时间戳（毫秒）→ 像素 X（latestTs 和 windowMs 由调用方传入，避免重复 snapshot）
double RealTimeChart::timeToPixelX(uint64_t timestamp, qint64 latestTs, double windowMs, double offset) const
{
    if (windowMs <= 0) return 50.0;

    qint64 delta = latestTs - static_cast<qint64>(timestamp);
    if (delta < 0) delta = 0;   // 时钟回拨：未来数据置右边缘
    double ratio = (static_cast<double>(delta) - offset) / windowMs;
    return 50.0 + (1.0 - ratio) * (static_cast<double>(width()) - 70.0);
}

// 通道值 → 像素 Y
double RealTimeChart::valueToPixelY(double value, double yMin, double yMax) const
{
    if (qFuzzyCompare(yMax, yMin))
        return (height() - 40.0) / 2.0;   // 所有值相同 → 居中
    double ratio = (value - yMin) / (yMax - yMin);
    return (height() - 40.0) - ratio * (height() - 60.0);
}

void RealTimeChart::drawBackground(QPainter &p)
{

    int w = width();
    int h = height();

    // ── 绘图区域边界 ──
    int left   = 50;    // Y 轴
    int right  = w - 20;
    int top    = 20;
    int bottom = h - 40;

    // ── 网格线（浅灰） ──
    p.setPen(QPen(QColor(0xE0, 0xE0, 0xE0), 0.5));  // 浅灰 0.5px

    // 水平网格（5 条）
    for (int i = 0; i <= 4; ++i) {
        double y = top + (bottom - top) * i / 4.0;
        p.drawLine(left, static_cast<int>(y), right, static_cast<int>(y));
    }

    // 垂直网格（5 条）
    for (int i = 0; i <= 4; ++i) {
        double x = left + (right - left) * i / 4.0;
        p.drawLine(static_cast<int>(x), top, static_cast<int>(x), bottom);
    }

    // ── 坐标轴（黑色 1.5px） ──
    p.setPen(QPen(Qt::black, 1.5));
    p.drawLine(left, bottom, right, bottom);   // X 轴
    p.drawLine(left, top, left, bottom);        // Y 轴

    // ── Y 轴刻度数字 + 标签 ──
    p.setPen(Qt::black);
    QFont smallFont = p.font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);

    // Y 轴刻度（匹配自适应范围）
    double yRange = m_curYMax - m_curYMin;
    if (yRange <= 0) yRange = 1024;
    for (int i = 0; i <= 4; ++i) {
        double val = m_curYMin + yRange * (4 - i) / 4.0;
        double y   = top + (bottom - top) * i / 4.0;
        p.drawText(2, static_cast<int>(y) + 4,
                   QString::number(val, 'f', (yRange < 10) ? 1 : 0));
    }
}
RealTimeChart::RealTimeChart(QWidget *parent):QWidget(parent)
{
    //设置最小尺寸+背景色
    setMinimumSize(400,200);
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window,Qt::white);
    setPalette(pal);

    //创建刷新定时器
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(40);   // 25 FPS
    connect(m_refreshTimer, &QTimer::timeout,
            this, &RealTimeChart::onRefresh);
    m_refreshTimer->start();

}

void RealTimeChart::setDataBuffer(DataBuffer *buffer)
{
    m_buffer = buffer;
}

void RealTimeChart::setCurrentTime(qint64 t)
{
    m_currentTime = t;
}

void RealTimeChart::setTimeWindow(double seconds)
{
    m_timeWindow = seconds;
    if(m_timeWindow < 5.0) m_timeWindow = 5.0;
    if(m_timeWindow >120.0) m_timeWindow = 120.0;
    update();
}

double RealTimeChart::timeWindow() const
{
    return m_timeWindow;
}

void RealTimeChart::paintEvent(QPaintEvent *)
{
    if (width() < 60 || height() < 40) return;

    // ── 离屏画布 ──
    if (m_offscreen.size() != size())
        m_offscreen = QPixmap(size());
    m_offscreen.fill(Qt::white);

    QPainter p(&m_offscreen);

    // ① 先算 Y 轴范围（背景和曲线共用，避免不同步）
    computeYRange();

    // ② 背景 + 坐标轴
    p.setRenderHint(QPainter::Antialiasing, true);
    drawBackground(p);

    // ③ 曲线
    p.setRenderHint(QPainter::Antialiasing, false);
    drawCurves(p);

    // 图例
    {
        auto snap = m_buffer ? m_buffer->snapshot() : QVector<DataPoint>();
        int chCount = snap.isEmpty() ? 3 : snap[0].channels.size();
        drawLegend(p, chCount);
    }

    p.end();

    // 贴到屏幕
    QPainter screenPainter(this);
    screenPainter.drawPixmap(0, 0, m_offscreen);
}

void RealTimeChart::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0)
        m_timeWindow/=1.2;          //向上滚 -> 放大（时间范围减小）
    else
        m_timeWindow *= 1.2;
    // 限制范围
    if (m_timeWindow < 5.0)  m_timeWindow = 5.0;
    if (m_timeWindow > 120.0) m_timeWindow = 120.0;

    update();   // 触发 paintEvent 重绘
}

void RealTimeChart::mousePressEvent(QMouseEvent *event)
{
    if(event ->button() == Qt::LeftButton){
        m_dragging = true;
        m_lastMousePos = event->pos();
    }
}

void RealTimeChart::mouseMoveEvent(QMouseEvent *event)
{
    if(!m_dragging) return;
    // 右拖 → 看更新的数据，左拖 → 看更旧的数据
    double dx = event->pos().x() - m_lastMousePos.x();
    double msPerPixel = (m_timeWindow * 1000.0) / (width() - 70.0);
    m_xOffset += dx * msPerPixel;

    m_lastMousePos = event ->pos();
    update();
}

void RealTimeChart::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
}

void RealTimeChart::contextMenuEvent(QContextMenuEvent *event)
{
    m_timeWindow = 30.0;
    m_xOffset = 0.0;
    update();
}

void RealTimeChart::onRefresh()
{
    update();   // 异步排队绘制，不阻塞事件循环
}

void RealTimeChart::computeYRange()
{
    if (!m_buffer) return;

    auto snap = m_buffer->snapshot();
    if (snap.size() < 2) return;

    // ── 共享计算：右边界 + 窗口 + 偏移 ──────────────
    qint64 wallNow = QDateTime::currentMSecsSinceEpoch();
    if (m_currentTime > 0) {
        m_latestTs = m_currentTime;
        m_usedOffset = 0.0;
    } else {
        qint64 dataLatest = snap.last().timestamp;
        m_latestTs = (wallNow - static_cast<qint64>(dataLatest) < 10000)
                      ? wallNow : dataLatest;
        m_usedOffset = m_xOffset;
    }
    m_windowMs = m_timeWindow * 1000.0;
    m_minTs    = m_latestTs - static_cast<qint64>(m_windowMs + m_usedOffset);
    if (m_minTs < 0) m_minTs = 0;

    // ── Y 轴范围 ────────────────────────────────────
    int channels = snap[0].channels.size();
    double yMin = 0, yMax = 1024;
    bool first = true;
    auto it = std::lower_bound(snap.begin(), snap.end(), m_minTs,
        [](const DataPoint &dp, uint64_t ts) { return dp.timestamp < ts; });
    for (; it != snap.end(); ++it) {
        for (int ch = 0; ch < channels && ch < 8; ++ch) {
            double v = it->channels[ch];
            if (first) { yMin = yMax = v; first = false; }
            else { if (v < yMin) yMin = v; if (v > yMax) yMax = v; }
        }
    }
    if (yMax > yMin) {
        double pad = (yMax - yMin) * 0.1;
        yMin -= pad; yMax += pad;
    }
    m_curYMin = yMin;
    m_curYMax = yMax;
}

void RealTimeChart::drawCurves(QPainter &p)
{
    if (!m_buffer) return;

    auto snap = m_buffer->snapshot();
    if (snap.size() < 2) return;

    int channels = snap[0].channels.size();

    // ── Y 轴范围（已由 computeYRange 预先计算）───────
    double yMin = m_curYMin, yMax = m_curYMax;



    for (int ch = 0; ch < channels && ch < 8; ++ch) {
        QPolygonF polyline;
        double lastPx = -9999;
        double decimateThreshold = 1.5;

        auto it = std::lower_bound(snap.begin(), snap.end(), m_minTs,
            [](const DataPoint &dp, uint64_t ts) { return dp.timestamp < ts; });
        for (; it != snap.end(); ++it) {
            const auto &dp = *it;

            double px = timeToPixelX(dp.timestamp, m_latestTs, m_windowMs, m_usedOffset);
            double py = valueToPixelY(dp.channels[ch], yMin, yMax);

            if (!polyline.isEmpty() && qAbs(px - lastPx) < decimateThreshold) continue;
            lastPx = px;
            polyline.append(QPointF(px, py));
        }

        p.setPen(QPen(CH_COLORS[ch], 1.0));
        p.drawPolyline(polyline);
    }
}

void RealTimeChart::drawLegend(QPainter &p, int channels)
{
    if (channels > 8) channels = 8;

    int x = width() -120;   //右上角起始位置
    int y =25;

    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);

    for(int i = 0; i <channels;++i){
        //色块12x12
        p.setPen(Qt::NoPen);
        p.setBrush(CH_COLORS[i]);
        p.drawRect(x,y,12,12);

        //文字
        p.setPen(Qt::black);
        p.drawText(x + 16, y + 10, QString("CH%1").arg(i));

        y += 16;   // 下一个图例往下排
    }
}
