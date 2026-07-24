#include "RealTimeChart.h"
#include <algorithm>
#include <QDateTime>
#include <QElapsedTimer>

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

    // ── 网格线 ──
    QColor gridColor  = m_darkMode ? QColor(0x3E,0x3E,0x3E) : QColor(0xE0,0xE0,0xE0);
    QColor axisColor  = m_darkMode ? QColor(0x88,0x88,0x88) : Qt::black;
    QColor textColor  = m_darkMode ? QColor(0xDC,0xDC,0xDC) : Qt::black;
    QColor bgColor    = m_darkMode ? QColor(0x1E,0x1E,0x1E) : Qt::white;

    p.fillRect(left, top, right-left, bottom-top, bgColor);

    p.setPen(QPen(gridColor, 0.5));
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

    // ── 坐标轴 ──
    p.setPen(QPen(axisColor, 1.5));
    p.drawLine(left, bottom, right, bottom);   // X 轴
    p.drawLine(left, top, left, bottom);        // Y 轴

    // ── Y 轴刻度 ──
    p.setPen(textColor);
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

void RealTimeChart::setDarkMode(bool dark)
{
    m_darkMode = dark;
    // 更新背景色
    QPalette pal = palette();
    pal.setColor(QPalette::Window, dark ? QColor(0x1E,0x1E,0x1E) : Qt::white);
    setPalette(pal);
    update();
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

    QElapsedTimer t; t.start();  // ── 性能计时 ──

    if (m_offscreen.size() != size())
        m_offscreen = QPixmap(size());
    m_offscreen.fill(m_darkMode ? QColor(0x1E,0x1E,0x1E) : Qt::white);

    QPainter p(&m_offscreen);

    computeYRange();
    qint64 tY = t.elapsed();

    p.setRenderHint(QPainter::Antialiasing, true);
    drawBackground(p);
    qint64 tBg = t.elapsed();

    p.setRenderHint(QPainter::Antialiasing, false);
    drawCurves(p);
    qint64 tCurve = t.elapsed();

    {
        auto snap = m_buffer ? m_buffer->snapshot() : QVector<DataPoint>();
        int chCount = snap.isEmpty() ? 3 : snap[0].channels.size();
        drawLegend(p, chCount);
    }

    p.end();

    QPainter screenPainter(this);
    screenPainter.drawPixmap(0, 0, m_offscreen);
    qint64 tTotal = t.elapsed();

    // 每 100 帧打印一次分段耗时 (ms) → 仅 Debug 构建
#ifndef QT_NO_DEBUG
    static int fc = 0;
    if (++fc % 100 == 0) {
        qDebug() << "⏱ paint Y:" << tY << "ms Bg:" << (tBg-tY) << "ms Curve:" << (tCurve-tBg)
                 << "ms total:" << tTotal << "ms";
    }
#endif
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
        auto snap = m_buffer ? m_buffer->snapshot() : QVector<DataPoint>();
        int chCount = snap.isEmpty() ? 0 : snap[0].channels.size();
        int ch = hitTestLegend(event->pos(), chCount);
        if (ch >= 0) {
            if (ch < m_chVisible.size())
                m_chVisible[ch] = !m_chVisible[ch];
            update();
            return;
        }
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

    int channels = snap[0].channels.size();

    // ── 初始化可见性 + Y 轴数组 ──
    if (m_chVisible.size() < channels) {
        int oldVis = m_chVisible.size(); m_chVisible.resize(channels);
        for (int i = oldVis; i < channels; ++i)
            m_chVisible[i] = true;
    }
    m_chYMin.resize(channels);
    m_chYMax.resize(channels);

    // ── 共享计算：右边界 + 窗口 + 偏移 ──
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

    // ── 每通道独立 Y 范围 ──
    auto it = std::lower_bound(snap.begin(), snap.end(), m_minTs,
        [](const DataPoint &dp, uint64_t ts) { return dp.timestamp < ts; });
    for (int ch = 0; ch < channels && ch < 16; ++ch) {
        double cMin = 0, cMax = 1024;
        bool first = true;
        for (auto jt = it; jt != snap.end(); ++jt) {
            double v = jt->channels[ch];
            if (first) { cMin = cMax = v; first = false; }
            else { if (v < cMin) cMin = v; if (v > cMax) cMax = v; }
        }
        if (cMax > cMin) {
            double pad = (cMax - cMin) * 0.1;
            m_chYMin[ch] = cMin - pad;
            m_chYMax[ch] = cMax + pad;
        } else {
            m_chYMin[ch] = cMin - 10;
            m_chYMax[ch] = cMax + 10;
        }
    }

    // 全局 Y 保留兼容（用于共享 Y 轴的背景绘制）
    double yMin = 0, yMax = 1024;
    bool firstGlobal = true;
    for (int ch = 0; ch < channels && ch < 16; ++ch) {
        if (!m_chVisible[ch]) continue;
        if (firstGlobal) { yMin = m_chYMin[ch]; yMax = m_chYMax[ch]; firstGlobal = false; }
        else {
            if (m_chYMin[ch] < yMin) yMin = m_chYMin[ch];
            if (m_chYMax[ch] > yMax) yMax = m_chYMax[ch];
        }
    }
    if (!firstGlobal) { m_curYMin = yMin; m_curYMax = yMax; }
}

void RealTimeChart::drawCurves(QPainter &p)
{
    if (!m_buffer) return;

    auto snap = m_buffer->snapshot();
    if (snap.size() < 2) return;

    int channels = snap[0].channels.size();

    // ── Y 轴范围（已由 computeYRange 预先计算）───────
    



        for (int ch = 0; ch < channels && ch < 16; ++ch) {
        if (ch < m_chVisible.size() && !m_chVisible[ch]) continue;
        double cYMin = (ch < m_chYMin.size()) ? m_chYMin[ch] : m_curYMin;
        double cYMax = (ch < m_chYMax.size()) ? m_chYMax[ch] : m_curYMax;
        QPolygonF polyline;
        double lastPx = -9999;
        double decimateThreshold = 1.5;

        auto it = std::lower_bound(snap.begin(), snap.end(), m_minTs,
            [](const DataPoint &dp, uint64_t ts) { return dp.timestamp < ts; });
        for (; it != snap.end(); ++it) {
            const auto &dp = *it;

            double px = timeToPixelX(dp.timestamp, m_latestTs, m_windowMs, m_usedOffset);
            double py = valueToPixelY(dp.channels[ch], cYMin, cYMax);

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
    if (channels > 16) channels = 16;

    int x = width() - 130;
    int y = 25;

    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);

    for (int i = 0; i < channels; ++i) {
        bool visible = (i < m_chVisible.size()) ? m_chVisible[i] : true;

        // 色块（可见=实心，隐藏=空心）
        p.setPen(Qt::NoPen);
        p.setBrush(CH_COLORS[i]);
        if (visible) {
            p.drawRect(x, y, 12, 12);           // 实心色块
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(CH_COLORS[i]);
            p.drawRect(x, y, 12, 12);           // 空心轮廓
        }

        // 文字（隐藏=灰字 + 删除线）
        QColor textColor = m_darkMode ? QColor(0xDC, 0xDC, 0xDC) : Qt::black;
        if (!visible) textColor = QColor(128, 128, 128);
        p.setPen(textColor);
        QString label = QString("CH%1").arg(i);
        if (!visible) label = QString("CH%1  ✗").arg(i);  // 显式隐藏标记
        p.drawText(x + 16, y + 10, label);

        y += 16;
    }
}

int RealTimeChart::hitTestLegend(const QPoint &pos, int channels) const
{
    if (channels > 16) channels = 16;
    int x = width() - 130;
    int y = 25;
    for (int i = 0; i < channels; ++i) {
        QRect r(x, y, 110, 16);
        if (r.contains(pos)) return i;
        y += 16;
    }
    return -1;
}
