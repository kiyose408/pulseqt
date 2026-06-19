//==============================================================================
// DataBuffer 实现 — 环形缓冲区
//
// 环形缓冲示意图（maxSize=5，已写入 7 条数据）：
//
//   写入前:  [D5][D6][D2][D3][D4]
//             ↑              ↑
//           m_head=2      (2+5-1)%5=1  ← 最旧数据 D2
//
//   写入 D7:  m_ring[2]=D7, m_head=3
//   新状态:   [D5][D6][D7][D3][D4]
//             ↑         ↑
//           最旧 D3   m_head=3
//
// snapshot() 遍历方向：从最旧 (m_head) 顺时针绕回到最新 (m_head-1)
//==============================================================================

#include "DataBuffer.h"

DataBuffer::DataBuffer(int maxSize, QObject *parent)
    : QObject(parent)
    , m_maxSize(maxSize)
{
    // 预分配 vector 容量——避免 push 过程中动态扩容
    m_ring.resize(m_maxSize);
}

//==============================================================================
// push — 写入数据点（加锁，线程安全）
//==============================================================================

void DataBuffer::push(const DataPoint &point)
{
    QMutexLocker locker(&m_mutex);

    m_ring[m_head] = point;
    m_head = (m_head + 1) % m_maxSize;
    if (m_count < m_maxSize)
        m_count++;

    emit bufferUpdated(1);
}

//==============================================================================
// snapshot — 返回当前数据的副本（加锁，线程安全）
//
// 重要：在锁内拷贝一份，解锁后返回副本。
//       调用方拿到的是独立副本，遍历时不会和写入线程冲突。
//==============================================================================

QVector<DataPoint> DataBuffer::snapshot() const
{
    QMutexLocker locker(&m_mutex);
    QVector<DataPoint> result;
    result.reserve(m_count);

    if (m_count == 0)
        return result;

    if (m_count < m_maxSize) {
        // ── 缓冲区未满：数据在 [0, m_head-1] 区间内顺序排列 ──
        //    例: m_head=3, m_count=3 → [D0, D1, D2]
        for (int i = 0; i < m_head; ++i)
            result.append(m_ring[i]);
    } else {
        // ── 缓冲区已满：数据从 m_head 绕回 ──
        //    最旧数据在 m_head，顺时针读 m_maxSize 个
        for (int i = 0; i < m_maxSize; ++i) {
            int idx = (m_head + i) % m_maxSize;
            result.append(m_ring[idx]);
        }
    }

    return result;   // 锁在这里自动释放
}

//==============================================================================
// size / clear
//==============================================================================

int DataBuffer::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_count;
}

void DataBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_head  = 0;
    m_count = 0;
    // 不需要清空 m_ring 内容——数据会被覆盖
}
