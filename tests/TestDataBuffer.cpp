//==============================================================================
// TestDataBuffer — DataBuffer 环形缓冲区单元测试
//
// 覆盖：
//   ① push 不满容量 → size 递增
//   ② push 超出容量 → 绕回，size == capacity
//   ③ snapshot 未满 → 顺序正确
//   ④ snapshot 跨绕回点 → 最旧→最新
//   ⑤ snapshot 空缓冲 → isEmpty
//   ⑥ clear 清空 → 可再写入
//   ⑦ bufferUpdated 信号
//   ⑧ 多线程压测 → 2 写 + 并发读，无崩溃
//==============================================================================

#include <QtTest>
#include <QThread>
#include "DataBuffer.h"

class TestDataBuffer : public QObject
{
    Q_OBJECT

private:
    // 辅助：创建带时间戳的 DataPoint
    static DataPoint makePoint(uint64_t ts, double ch0 = 0.0, double ch1 = 0.0)
    {
        DataPoint dp;
        dp.timestamp = ts;
        dp.channels = {ch0, ch1};
        return dp;
    }

private slots:
    // ────────────────────────────────────────────────────
    // ① push 不满容量 → size() 正确递增，snapshot 顺序正确
    // ────────────────────────────────────────────────────
    void pushUnderCapacity()
    {
        DataBuffer buffer(10);
        for (int i = 0; i < 5; ++i) {
            buffer.push(makePoint(i * 100, double(i), double(i * 2)));
        }
        QCOMPARE(buffer.size(), 5);

        QVector<DataPoint> snap = buffer.snapshot();
        QCOMPARE(snap.size(), 5);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(snap[i].timestamp, uint64_t(i * 100));
            QCOMPARE(snap[i].channels[0], double(i));
        }
    }

    // ────────────────────────────────────────────────────
    // ② push 超出容量 → 头指针绕回，旧数据被覆盖
    //    容量 5，写入 8 条 → size == 5
    // ────────────────────────────────────────────────────
    void pushOverCapacity()
    {
        DataBuffer buffer(5);
        for (int i = 0; i < 8; ++i)
            buffer.push(makePoint(i));

        QCOMPARE(buffer.size(), 5);
    }

    // ────────────────────────────────────────────────────
    // ③ snapshot 未满：数据在 [0, m_head-1]，顺序 = 插入顺序
    // ────────────────────────────────────────────────────
    void snapshotNotFull()
    {
        DataBuffer buffer(10);
        for (int i = 0; i < 4; ++i)
            buffer.push(makePoint(i));

        QVector<DataPoint> snap = buffer.snapshot();
        QCOMPARE(snap.size(), 4);
        QCOMPARE(snap[0].timestamp, uint64_t(0));
        QCOMPARE(snap[3].timestamp, uint64_t(3));
    }

    // ────────────────────────────────────────────────────
    // ④ snapshot 跨绕回点：容量 5，写入 8 条 → 存留 [3,4,5,6,7]
    //    snapshot 应从最旧到最新
    // ────────────────────────────────────────────────────
    void snapshotWrapped()
    {
        DataBuffer buffer(5);
        for (int i = 0; i < 8; ++i)
            buffer.push(makePoint(i));

        QVector<DataPoint> snap = buffer.snapshot();
        QCOMPARE(snap.size(), 5);
        // 最旧的 5 条是 3,4,5,6,7
        for (int i = 0; i < 5; ++i)
            QCOMPARE(snap[i].timestamp, uint64_t(3 + i));
    }

    // ────────────────────────────────────────────────────
    // ⑤ 空缓冲区 snapshot
    // ────────────────────────────────────────────────────
    void snapshotEmpty()
    {
        DataBuffer buffer(100);
        QCOMPARE(buffer.size(), 0);
        QVERIFY(buffer.snapshot().isEmpty());
    }

    // ────────────────────────────────────────────────────
    // ⑥ clear 清空 → 可再写入
    // ────────────────────────────────────────────────────
    void clearBuffer()
    {
        DataBuffer buffer(10);
        for (int i = 0; i < 5; ++i)
            buffer.push(makePoint(i));
        QCOMPARE(buffer.size(), 5);

        buffer.clear();
        QCOMPARE(buffer.size(), 0);
        QVERIFY(buffer.snapshot().isEmpty());

        // 清空后可正常写入
        buffer.push(makePoint(42));
        QCOMPARE(buffer.size(), 1);
        QCOMPARE(buffer.snapshot()[0].timestamp, uint64_t(42));
    }

    // ────────────────────────────────────────────────────
    // ⑦ bufferUpdated 信号
    // ────────────────────────────────────────────────────
    void signalBufferUpdated()
    {
        DataBuffer buffer(10);
        QSignalSpy spy(&buffer, &DataBuffer::bufferUpdated);

        buffer.push(DataPoint{});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
    }

    // ────────────────────────────────────────────────────
    // ⑧ 多线程压测：2 写线程 + 主线程并发 snapshot
    //    验证无 crash、size 不超过 capacity
    // ────────────────────────────────────────────────────
    void multithreadedStress()
    {
        const int CAPACITY   = 100;
        const int PER_THREAD = 5000;

        DataBuffer buffer(CAPACITY);

        // ── Writer 1 ──
        QThread t1;
        QObject::connect(&t1, &QThread::started, [&]() {
            for (int i = 0; i < PER_THREAD; ++i)
                buffer.push(makePoint(i));
            t1.quit();
        });

        // ── Writer 2 ──
        QThread t2;
        QObject::connect(&t2, &QThread::started, [&]() {
            for (int i = 0; i < PER_THREAD; ++i)
                buffer.push(makePoint(1'000'000 + i));
            t2.quit();
        });

        t1.start();
        t2.start();

        // ── Reader（主线程）：持续 snapshot ──
        int reads = 0;
        while (t1.isRunning() || t2.isRunning()) {
            QVector<DataPoint> snap = buffer.snapshot();
            QVERIFY(snap.size() <= CAPACITY);
            ++reads;
        }

        t1.wait();
        t2.wait();

        QCOMPARE(buffer.size(), CAPACITY);
        qInfo() << "Multithreaded: pushed" << (PER_THREAD * 2)
                << "read" << reads << "times, size =" << buffer.size();
    }
};

QTEST_MAIN(TestDataBuffer)
#include "TestDataBuffer.moc"
