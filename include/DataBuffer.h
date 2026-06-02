//==============================================================================
// DataBuffer - 环形缓冲区（线程安全）
//
// 预分配 QVector + 头指针绕回。写入 O(1)，snapshot O(n)。
// QMutex 保护所有写操作，读取通过 snapshot() 返回副本。
//==============================================================================

#ifndef DATABUFFER_H
#define DATABUFFER_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include "DataPoint.h"

class DataBuffer : public QObject
{
    Q_OBJECT

public:
    // maxSize: 环形缓冲区容量（默认 10000）
    explicit DataBuffer(int maxSize = 10000, QObject *parent = nullptr);

    // 写入一个数据点（线程安全，会加锁）
    void push(const DataPoint &point);

    // 返回当前所有数据的副本（线程安全，持有锁时拷贝一份，然后解锁返回）
    // ⚠️ 返回的是副本，遍历时不需要额外锁
    QVector<DataPoint> snapshot() const;

    // 当前缓冲区中的数据量（≤ maxSize）
    int size() const;

    // 清空缓冲区
    void clear();

signals:
    // 每次 push 后发送，count = 本次新增条数
    void bufferUpdated(int count);

private:
    mutable QMutex m_mutex;          // mutable: const 方法 snapshot() 也要加锁
    QVector<DataPoint> m_ring;       // 预分配环形数组
    int m_head   = 0;                // 下一次写入的位置
    int m_count  = 0;                // 当前数据量（≤ m_maxSize）
    int m_maxSize;                   // 缓冲容量
};

#endif // DATABUFFER_H
