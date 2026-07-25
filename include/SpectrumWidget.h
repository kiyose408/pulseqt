//==============================================================================
// SpectrumWidget — 实时频谱（FFT）
//
// 从 DataBuffer 取最近 N 点 → Radix-2 FFT → 绘制柱状频谱。
// 每通道独立频谱，图例行点击切换选中/显隐。
//==============================================================================

#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QWidget>
#include <QTimer>
#include "DataBuffer.h"
#include "ChannelColors.h"

/**
 * @brief 实时 FFT 频谱面板
 *
 * 25FPS 定时刷新，256 点 Radix-2 FFT。
 * 网格 + Y 轴% + X 轴 Hz 标注。
 * 图例交互：点击选中通道（白边框），再点隐藏（斜线+删除线）。
 */
class SpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    /// @brief 绑定数据源
    void setDataBuffer(DataBuffer *buffer);
    /// @brief 设置 FFT 点数（自动向上取 2 的幂）
    void setFFTSize(int n);
    /// @brief 设置当前通道
    void setChannel(int ch);
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
};

#endif // SPECTRUMWIDGET_H
