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
class RealTimeChart : public QWidget {
    Q_OBJECT
public:
    explicit RealTimeChart(QWidget *parent = nullptr);

    void setDataBuffer(DataBuffer *buffer);
    void setTimeWindow(double seconds);
    void setCurrentTime(qint64 t);
    void setDarkMode(bool dark);            // 暗色主题
    double timeWindow() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event)override;
    void mousePressEvent(QMouseEvent *event)override;
    void mouseMoveEvent(QMouseEvent *event)override;
    void mouseReleaseEvent(QMouseEvent *event)override;
    void contextMenuEvent(QContextMenuEvent *event)override;

private slots:
    void onRefresh();           //定时器刷新

private:
    double timeToPixelX(uint64_t timestamp, qint64 latestTs, double windowMs, double offset = 0.0) const;
    double valueToPixelY(double value, double yMin, double yMax) const;
    void computeYRange();                       //预先计算时间/范围（背景+曲线共用）
    void drawBackground(QPainter &p);           //网格＋坐标轴
    void drawCurves (QPainter &p);              //曲线
    void drawLegend(QPainter &p, int channels);  //图例

    DataBuffer *m_buffer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QPixmap m_offscreen;
    double m_timeWindow = 30.0;         // x轴跨度（秒）
    double m_xOffset = 0.0 ;            //拖拽偏移（毫秒）
    bool   m_darkMode = false;          // 暗色主题
    double m_curYMin = 0.0;             // 当前 Y 轴下限
    double m_curYMax = 1024.0;          // 当前 Y 轴上限
    qint64 m_latestTs = 0;              // 预计算：右边界时间戳
    double m_usedOffset = 0.0;          // 预计算：有效偏移
    double m_windowMs = 30000.0;        // 预计算：窗口(ms)
    qint64 m_minTs = 0;                 // 预计算：可视左边界
    bool m_dragging =false;
    qint64 m_currentTime = 0;           //回放参考时间（>0=回放模式）
    QPoint m_lastMousePos;

    static const QColor CH_COLORS[16];

};

#endif // REALTIMECHART_H
