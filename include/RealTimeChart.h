//==============================================================================
// RealTimeChart — 自绘实时曲线
//==============================================================================
//
// 功能：双缓冲自绘、抽稀、多Y轴、图例点击显隐、滚轮缩放、拖拽平移、暗色主题。
//
// 绘制流程（paintEvent）：
//   computeYRange (每通道独立 Y) → drawBackground (网格+轴) → drawCurves (抽稀+绘制)
//   → drawLegend (图例，点击切换显隐)
//
// 抽稀：decimateThreshold=1.5px，x 间距小于阈值的点合并，保证 30s 窗口不卡顿。
//==============================================================================

#ifndef REALTIMECHART_H
#define REALTIMECHART_H

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QPolygonF>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include "DataBuffer.h"
#include "ChannelColors.h"

/// @brief 双缓冲自绘实时曲线
///
/// 核心优化：
/// - QPixmap 双缓冲减少闪烁
/// - 抽稀（1.5px 阈值）降低 30s 窗口 3000 点的绘制量
/// - 每通道独立 Y 轴范围（多Y轴效果）
/// - 图例行点击切换通道显隐
class RealTimeChart : public QWidget {
    Q_OBJECT
public:
    explicit RealTimeChart(QWidget *parent = nullptr);

    /// @brief 设置数据源（来自 ParseWorker::buffer()）
    void setDataBuffer(DataBuffer *buffer);

    /// @brief 设置 X 轴时间窗口（秒），默认 30
    /// @param seconds 秒数，滚轮缩放会修改此值
    void setTimeWindow(double seconds);

    /// @brief 设置回放参考时间（>0=回放模式，0=实时模式）
    void setCurrentTime(qint64 t);

    /// @brief 切换暗色主题
    void setDarkMode(bool dark);

    /// @brief 返回当前时间窗口
    double timeWindow() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    /// @brief 定时器刷新（25FPS）
    void onRefresh();

private:
    double timeToPixelX(uint64_t timestamp, qint64 latestTs, double windowMs, double offset = 0.0) const;
    double valueToPixelY(double value, double yMin, double yMax) const;
    void computeYRange();
    void drawBackground(QPainter &p);
    void drawCurves(QPainter &p);
    void drawLegend(QPainter &p, int channels);
    int  hitTestLegend(const QPoint &pos, int channels) const;

    DataBuffer *m_buffer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QPixmap m_offscreen;                 // 双缓冲画布
    double m_timeWindow = 30.0;          // X 轴时间跨度（秒）
    double m_xOffset = 0.0;              // 拖拽偏移（毫秒）
    bool   m_darkMode = false;           // 暗色主题
    double m_curYMin = 0.0;              // 全局 Y 轴下限（背景网格用）
    double m_curYMax = 1024.0;           // 全局 Y 轴上限
    qint64 m_latestTs = 0;               // 右边界时间戳
    double m_usedOffset = 0.0;           // 有效偏移
    double m_windowMs = 30000.0;         // 窗口毫秒数
    qint64 m_minTs = 0;                  // 可视左边界
    bool m_dragging = false;
    qint64 m_currentTime = 0;            // 回放参考时间
    QPoint m_lastMousePos;

    QVector<bool>   m_chVisible;         // 通道显隐
    QVector<double> m_chYMin, m_chYMax;  // 每通道独立 Y 轴范围
};

#endif // REALTIMECHART_H
