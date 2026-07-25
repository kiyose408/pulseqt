//==============================================================================
// SpectrumWidget — 实时频谱（FFT）
//
// 从 DataBuffer 取最近 N 点 → FFT → 绘制柱状频谱
// 每通道独立频谱，图例行点击切换
//==============================================================================

#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QWidget>
#include <QTimer>
#include "DataBuffer.h"
#include "ChannelColors.h"

class SpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setDataBuffer(DataBuffer *buffer);
    void setFFTSize(int n);          // FFT 点数 (默认 256，须 2 的幂)
    void setChannel(int ch);         // 切换当前显示通道
    int  currentChannel() const { return m_currentCh; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onRefresh();

private:
    int hitTestChannel(const QPoint &pos, int channels) const;

    DataBuffer *m_buffer = nullptr;
    QTimer     *m_timer  = nullptr;
    int         m_fftSize = 256;
    int         m_currentCh = 0;
    int         m_totalCh   = 0;
    QVector<bool> m_chVisible;

    // CH_COLORS 定义在 ChannelColors.h（与 RealTimeChart 共享）
};

#endif // SPECTRUMWIDGET_H
