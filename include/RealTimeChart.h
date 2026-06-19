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
    void setTimeWindow(double seconds);     //X轴时间跨度（默认30s）
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
    double timeToPixelX(uint64_t timestamp, qint64 latestTs, double windowMs) const;
    double valueToPixelY(double value, double yMin, double yMax) const;
    void drawBackground(QPainter &p);           //网格＋坐标轴
    void drawCurves (QPainter &p);              //曲线
    void drawLegend(QPainter &p, int channels);  //图例

    DataBuffer *m_buffer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QPixmap m_offscreen;
    double m_timeWindow = 30.0;         // x轴跨度（秒）
    double m_xOffset = 0.0 ;            //拖拽偏移（毫秒）
    bool m_dragging =false;
    QPoint m_lastMousePos;

    static const QColor CH_COLORS[8];

};

#endif // REALTIMECHART_H
