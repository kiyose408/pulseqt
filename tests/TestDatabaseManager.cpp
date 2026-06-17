//==============================================================================
// TestDatabaseManager — DatabaseManager 单元测试
//
// 覆盖：
//   ① serializeChannels 往返
//   ② insert + flush → query 数据一致
//   ③ 超 batchSize(100) 自动分批次提交
//   ④ query 时间范围边界（BETWEEN 含端点）
//   ⑤ query LIMIT 截断
//   ⑥ cleanup 清理旧数据
//
// 隔离策略：每个用例用独立临时数据库，cleanup() 删除文件
//==============================================================================

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include "DatabaseManager.h"

// 前向声明 friend 需完整类型
class TestDatabaseManager : public QObject
{
    Q_OBJECT

private:
    QString          m_dbPath;
    DatabaseManager *m_mgr = nullptr;

    // 辅助：创建带时间戳的 DataPoint
    static DataPoint makePoint(uint64_t ts, double ch0 = 0.0)
    {
        DataPoint dp;
        dp.timestamp = ts;
        dp.channels = {ch0};
        return dp;
    }

private slots:
    // ── 整个测试类开始前 ────────────────────────────────
    void initTestCase()
    {
        m_dbPath = QDir::tempPath() + "/test_pulseqt_t024.db";
        QFile::remove(m_dbPath);  // 清理上次残留
    }

    // ── 每个用例前 ──────────────────────────────────────
    void init()
    {
        m_mgr = new DatabaseManager(this);
        QVERIFY(m_mgr->init(m_dbPath));
    }

    // ── 每个用例后 ──────────────────────────────────────
    void cleanup()
    {
        delete m_mgr;             // 析构 flush + close + removeDatabase
        m_mgr = nullptr;
        QFile::remove(m_dbPath);  // 物理删除，下个用例从零开始
    }

    // ────────────────────────────────────────────────────
    // ① channels 序列化往返（static 方法，不依赖 DB）
    // ────────────────────────────────────────────────────
    void serializeRoundtrip()
    {
        QVector<double> original = {1.5, -3.0, 42.0, 0.0, -1e9};

        QByteArray blob = m_mgr->serializeChannels(original);
        QVERIFY(!blob.isEmpty());

        QVector<double> restored = m_mgr->deserializeChannels(blob);
        QCOMPARE(restored.size(), original.size());
        for (int i = 0; i < original.size(); ++i)
            QCOMPARE(restored[i], original[i]);
    }

    // 空 channels 序列化往返
    void serializeEmptyChannels()
    {
        QVector<double> empty;
        QByteArray blob = m_mgr->serializeChannels(empty);
        QVector<double> restored = m_mgr->deserializeChannels(blob);
        QVERIFY(restored.isEmpty());
    }

    // ────────────────────────────────────────────────────
    // ② insert + flush → query 返回正确数据
    // ────────────────────────────────────────────────────
    void insertAndQuery()
    {
        const int N = 20;
        for (int i = 0; i < N; ++i)
            m_mgr->insert(makePoint(i * 100, double(i)));

        QCOMPARE(m_mgr->rowCount(), 0);  // 还没 flush
        m_mgr->flush();
        QCOMPARE(m_mgr->rowCount(), N);

        // 全范围查询
        auto results = m_mgr->query(0, (N - 1) * 100, 10000);
        QCOMPARE(results.size(), N);

        // 验证每条
        for (int i = 0; i < N; ++i) {
            QCOMPARE(results[i].timestamp, uint64_t(i * 100));
            QCOMPARE(results[i].channels.size(), 1);
            QCOMPARE(results[i].channels[0], double(i));
        }
    }

    // ────────────────────────────────────────────────────
    // ③ 超 batchSize (100) 自动分批次提交
    //    插入 150 条，不调 flush，验证自动提交了两批
    // ────────────────────────────────────────────────────
    void autoCommitBatch()
    {
        const int N = 150;
        for (int i = 0; i < N; ++i)
            m_mgr->insert(makePoint(i));

        // insert 应在第 100 条时自动 flush 一批
        int rows = m_mgr->rowCount();
        QVERIFY2(rows >= 100, qPrintable(QString("Expected >= 100, got %1").arg(rows)));

        // 手动 flush 剩余 50 条
        m_mgr->flush();
        QCOMPARE(m_mgr->rowCount(), N);
    }

    // ────────────────────────────────────────────────────
    // ④ query 时间范围边界（BETWEEN 含两端）
    // ────────────────────────────────────────────────────
    void queryBoundary()
    {
        for (int i = 0; i < 100; ++i)
            m_mgr->insert(makePoint(i));
        m_mgr->flush();

        // tBegin=20, tEnd=50 → 返回 31 条 (20~50 含端点)
        auto results = m_mgr->query(20, 50, 10000);
        QCOMPARE(results.size(), 31);
        QCOMPARE(results.first().timestamp, uint64_t(20));
        QCOMPARE(results.last().timestamp,  uint64_t(50));
    }

    // 查询空范围（tBegin > tEnd）
    void queryEmptyRange()
    {
        for (int i = 0; i < 10; ++i)
            m_mgr->insert(makePoint(i));
        m_mgr->flush();

        auto results = m_mgr->query(5, 3, 10000);
        QCOMPARE(results.size(), 0);
    }

    // ────────────────────────────────────────────────────
    // ⑤ query LIMIT 截断
    // ────────────────────────────────────────────────────
    void queryLimit()
    {
        for (int i = 0; i < 50; ++i)
            m_mgr->insert(makePoint(i));
        m_mgr->flush();

        auto results = m_mgr->query(0, 100, 10);
        QCOMPARE(results.size(), 10);
        QCOMPARE(results.first().timestamp, uint64_t(0));
        QCOMPARE(results.last().timestamp,  uint64_t(9));
    }

    // ────────────────────────────────────────────────────
    // ⑥ cleanup — 清理旧数据
    // ────────────────────────────────────────────────────
    void cleanupOldData()
    {
        // 写入一条"极旧"数据（timestamp 很小）
        DataPoint old;
        old.timestamp = 1;  // 1970-01-01 00:00:00.001
        old.channels  = {0.0};
        m_mgr->insert(old);
        m_mgr->flush();

        int before = m_mgr->rowCount();
        QVERIFY(before > 0);

        // cleanup(0) → "0 天前"即现在 → 删除所有数据
        int deleted = m_mgr->cleanup(0);
        QCOMPARE(deleted, before);
        QCOMPARE(m_mgr->rowCount(), 0);
    }

    // cleanup 保留新数据
    void cleanupKeepsRecent()
    {
        // 写入一条当前时间的数据
        DataPoint recent;
        recent.timestamp = QDateTime::currentMSecsSinceEpoch();
        recent.channels  = {42.0};
        m_mgr->insert(recent);
        m_mgr->flush();

        int before = m_mgr->rowCount();
        // cleanup(365) → 删除 365 天前的 → 当前数据保留
        int deleted = m_mgr->cleanup(365);
        QCOMPARE(deleted, 0);
        QCOMPARE(m_mgr->rowCount(), before);
    }
};

QTEST_MAIN(TestDatabaseManager)
#include "TestDatabaseManager.moc"
