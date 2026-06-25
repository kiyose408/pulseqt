//==============================================================================
// MagDataBuffer — 磁场值环形缓冲区（线程安全）
//
// 与 DataBuffer 相同模式：预分配 + 头指针绕回 + QMutex 保护。
// 独立实现，不依赖 DataBuffer，避免破坏 v1.x 已有代码。
//==============================================================================

#ifndef MAGDATABUFFER_H
#define MAGDATABUFFER_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include "MagDataPoint.h"

class MagDataBuffer : public QObject
{
    Q_OBJECT

public:
    explicit MagDataBuffer(int maxSize = 10000, QObject *parent = nullptr);

    void push(const MagDataPoint &point);
    QVector<MagDataPoint> snapshot() const;
    int size() const;
    void clear();

signals:
    void bufferUpdated(int count);

private:
    mutable QMutex m_mutex;
    QVector<MagDataPoint> m_ring;
    int m_head    = 0;
    int m_count   = 0;
    int m_maxSize;
};

#endif // MAGDATABUFFER_H
