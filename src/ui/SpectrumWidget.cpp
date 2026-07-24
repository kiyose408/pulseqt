//==============================================================================
// SpectrumWidget 实现
//==============================================================================

#include "SpectrumWidget.h"
#include "FFT.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>


SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    m_timer = new QTimer(this);
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, &SpectrumWidget::onRefresh);
    m_timer->start();
}

void SpectrumWidget::setDataBuffer(DataBuffer *buffer) { m_buffer = buffer; }

void SpectrumWidget::setFFTSize(int n)
{
    int p = 1; while (p < n) p <<= 1;
    m_fftSize = p;
}

void SpectrumWidget::setChannel(int ch)
{
    if (ch >= 0 && ch < m_totalCh) m_currentCh = ch;
}

void SpectrumWidget::onRefresh() { update(); }

void SpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1E, 0x1E, 0x1E));

    int w = width(), h = height();
    int plotLeft = 40, plotRight = w - 10, plotTop = 10, plotBottom = h - 25;

    // 网格
    p.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 1; i < 5; ++i) {
        int y = plotTop + (plotBottom - plotTop) * i / 5;
        p.drawLine(plotLeft, y, plotRight, y);
    }
    QFont f7 = p.font(); f7.setPointSize(7); p.setFont(f7);
    p.setPen(QColor(150, 150, 150));
    for (int i = 0; i <= 5; ++i)
        p.drawText(2, plotTop + (plotBottom - plotTop) * i / 5 + 4,
                   QString::number(100 - i * 20) + "%");

    // 无数据提示
    if (!m_buffer || m_buffer->snapshot().size() < m_fftSize) {
        p.setPen(QColor(100, 100, 100));
        p.drawText(rect(), Qt::AlignCenter, "等待数据... (需采集 2.5s)");
        return;
    }

    auto snap = m_buffer->snapshot();
    m_totalCh = snap[0].channels.size();
    if (m_chVisible.size() < m_totalCh) {
        int oldV = m_chVisible.size(); m_chVisible.resize(m_totalCh);
        for (int i = oldV; i < m_totalCh; ++i) m_chVisible[i] = true;
    }

    int bins = m_fftSize / 2 + 1;
    int showBins = qMin(bins - 1, 200);
    int plotH = plotBottom - plotTop;
    double barW = std::max(1.0, (double)(plotRight - plotLeft) / showBins);
    double maxHz = 50.0;

    // ── 柱子 ──
    for (int ch = 0; ch < m_totalCh; ++ch) {
        bool vis = (ch < m_chVisible.size()) ? m_chVisible[ch] : true;
        if (!vis) continue;  // 隐藏通道不画柱子

        std::vector<double> samples(m_fftSize);
        int start = qMax(0, snap.size() - m_fftSize);
        for (int j = 0; j < m_fftSize; ++j)
            samples[j] = snap[start + j].channels[ch];

        auto mag = FFT::computeMagnitude(samples);
        double maxMag = 0;
        for (int i = 1; i < bins && i <= showBins; ++i)
            if (mag[i] > maxMag) maxMag = mag[i];
        if (maxMag < 1.0) maxMag = 1.0;

        QColor bc = CH_COLORS[ch % 8];
        if (ch != m_currentCh) bc.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(bc);
        for (int i = 1; i <= showBins; ++i) {
            double barH = (mag[i] / maxMag) * plotH;
            if (barH < 2.0) barH = 2.0;
            p.drawRect(QRectF(plotLeft + (i - 1) * barW,
                              plotBottom - barH, barW - 1, barH));
        }
    }

    // ── 图例 ──（始终显示全部通道）
    for (int ch = 0; ch < m_totalCh; ++ch) {
        bool vis = (ch < m_chVisible.size()) ? m_chVisible[ch] : true;
        bool cur = (ch == m_currentCh);
        int lx = w - 110;
        int ly = 8 + ch * 16;
        QColor lc = CH_COLORS[ch % 8];

        if (vis) {
            p.setPen(Qt::NoPen);
            p.setBrush(lc);
            p.drawRect(lx, ly, 10, 10);
            if (cur) {
                p.setPen(QPen(Qt::white, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRect(lx, ly, 10, 10);
            }
        } else {
            p.setPen(QPen(lc.darker(150), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(lx, ly, 10, 10);
            p.drawLine(lx, ly, lx + 10, ly + 10);
        }
        p.setPen(cur ? Qt::white : QColor(180, 180, 180));
        QFont fb = p.font(); fb.setBold(cur);
        if (!vis) fb.setStrikeOut(true);
        p.setFont(fb);
        p.drawText(lx + 14, ly + 9, QString("CH%1").arg(ch));
        fb.setStrikeOut(false); p.setFont(fb);
    }

    // ── X 轴频率 ──
    p.setPen(QColor(150, 150, 150));
    QFont f6 = p.font(); f6.setPointSize(6); p.setFont(f6);
    for (int hz : {0, 10, 20, 30, 40, 50}) {
        int x = plotLeft + (showBins - 1) * barW * hz / maxHz;
        p.drawText(x - 10, plotBottom + 14, QString::number(hz) + "Hz");
    }
}

void SpectrumWidget::mousePressEvent(QMouseEvent *event)
{
    int ch = hitTestChannel(event->pos(), m_totalCh);
    if (ch < 0) return;

    if (ch == m_currentCh) {
        if (ch < m_chVisible.size())
            m_chVisible[ch] = !m_chVisible[ch];
    } else {
        m_currentCh = ch;
    }
    update();
}

int SpectrumWidget::hitTestChannel(const QPoint &pos, int channels) const
{
    for (int i = 0; i < channels; ++i) {
        QRect r(width() - 110, 8 + i * 16, 50, 14);
        if (r.contains(pos)) return i;
    }
    return -1;
}
