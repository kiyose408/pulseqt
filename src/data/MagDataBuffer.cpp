//==============================================================================
// MagDataBuffer 实现 — 环形缓冲区（线程安全）
//
// 模式与 DataBuffer 完全一致，存储类型从 DataPoint 换为 MagDataPoint。
//==============================================================================

#include "MagDataBuffer.h"

MagDataBuffer::MagDataBuffer(int maxSize, QObject *parent)
    : QObject(parent)
    , m_maxSize(maxSize)
{
    m_ring.resize(m_maxSize);
}

void MagDataBuffer::push(const MagDataPoint &point)
{
    QMutexLocker locker(&m_mutex);
    m_ring[m_head] = point;
    m_head = (m_head + 1) % m_maxSize;
    if (m_count < m_maxSize)
        m_count++;
    emit bufferUpdated(1);
}

QVector<MagDataPoint> MagDataBuffer::snapshot() const
{
    QMutexLocker locker(&m_mutex);
    QVector<MagDataPoint> result;
    result.reserve(m_count);

    if (m_count == 0)
        return result;

    if (m_count < m_maxSize) {
        for (int i = 0; i < m_head; ++i)
            result.append(m_ring[i]);
    } else {
        for (int i = 0; i < m_maxSize; ++i) {
            int idx = (m_head + i) % m_maxSize;
            result.append(m_ring[idx]);
        }
    }
    return result;
}

int MagDataBuffer::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_count;
}

void MagDataBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_head  = 0;
    m_count = 0;
}
