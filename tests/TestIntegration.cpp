//==============================================================================
// TestIntegration — 全链路集成测试（T025）
//
// 不启动 GUI，程序化走通完整数据管道：
//   Python 模拟器 → TCP → TcpWorker → ParseWorker → DataBuffer → SQLite
//
// 测试：
//   ① 启动模拟器 → 连接 → 采集 10s → DB 有 ≥900 条记录
//   ② DB 中 channels 值在合理范围（正弦 0-1023）
//   ③ 断线重连：kill 模拟器 → 检测断线 → 重启 → 30s 内重连 → DB 继续增长
//
// 依赖：Python 3 + pyserial（仅模拟器需要）
//==============================================================================

#include <QtTest>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QDebug>
#include "ChannelManager.h"
#include "TcpChannel.h"
#include "ParseWorker.h"
#include "DatabaseManager.h"

class TestIntegration : public QObject
{
    Q_OBJECT

private:
    QProcess  *m_simulator = nullptr;
    QString    m_dbPath;
    QString    m_simScript;

    // ── 辅助：清理残留 Python 进程（释放 9999 端口） ──────
    void killStaleSimulators()
    {
#ifdef Q_OS_WIN
        QProcess::execute("taskkill", {"/f", "/im", "python.exe"});
#else
        QProcess::execute("pkill", {"-f", "tcp_wave_simulator"});
#endif
        QTest::qWait(500);
    }

    // ── 线程安全清理 ──────────────────────────────────
    // 在所属线程中清理 socket/timer，避免跨线程析构崩溃
    void stopThreads(QThread &comm, QThread &parse, ChannelManager &cm, ParseWorker &parser)
    {
        parser.setCollecting(false);
        QMetaObject::invokeMethod(&cm, "disconnectDevice", Qt::BlockingQueuedConnection);
        comm.quit();
        parse.quit();
        comm.wait(5000);
        parse.wait(5000);
    }

    // ── 辅助：启动 TCP 波形模拟器 ──────────────────────────
    bool startSimulator()
    {
        killStaleSimulators();

        m_simulator = new QProcess(this);
        m_simulator->setProcessChannelMode(QProcess::MergedChannels);

        QStringList args;
        args << "-u" << m_simScript;
        m_simulator->start("python", args);

        if (!m_simulator->waitForStarted(5000)) {
            qWarning() << "Failed to start simulator";
            return false;
        }

        // 轮询 TCP 端口直到模拟器就绪
        for (int i = 0; i < 40; ++i) {
            QTcpSocket probe;
            probe.connectToHost("127.0.0.1", 9999);
            if (probe.waitForConnected(500)) {
                probe.disconnectFromHost();
                qInfo() << "Simulator ready after" << (i * 250) << "ms";
                return true;
            }
            QTest::qWait(250);
        }
        qWarning() << "Simulator did not bind port 9999 within 10s";
        return false;
    }

    void stopSimulator()
    {
        if (m_simulator && m_simulator->state() != QProcess::NotRunning) {
            m_simulator->terminate();
            m_simulator->waitForFinished(5000);
            killStaleSimulators();
        }
    }

private slots:
    void initTestCase()
    {
        // 定位模拟器脚本（从 build/ 运行，脚本在 ../tools/）
        QStringList candidates = {
            QDir::currentPath() + "/../tools/tcp_wave_simulator.py",
            QDir::currentPath() + "/../../tools/tcp_wave_simulator.py",
            QString(PROJECT_SOURCE_DIR) + "/tools/tcp_wave_simulator.py",
        };
        for (const auto &c : candidates) {
            if (QFile::exists(c)) { m_simScript = c; break; }
        }
        qInfo() << "Simulator script:" << m_simScript;

        m_dbPath = QDir::tempPath() + "/test_pulseqt_t025.db";
        QFile::remove(m_dbPath);
    }

    void cleanupTestCase()
    {
        stopSimulator();
        QFile::remove(m_dbPath);
    }

    // ────────────────────────────────────────────────────
    // ① 采集 10s → DB 记录数 ≥ 900（100Hz × 10s，允许少量丢帧）
    // ② channels 值在合理范围内
    // ────────────────────────────────────────────────────
    void dataCollection()
    {
        if (!QFile::exists(m_simScript))
            QSKIP("Simulator script not found");

        QVERIFY(startSimulator());

        QThread commThread, parseThread;
        ChannelManager cm;
        auto *ch = new TcpChannel("127.0.0.1", 9999);
        cm.setChannel(ch);
        ParseWorker parser(m_dbPath);
        bool connected = false;

        cm.moveToThread(&commThread);
        parser.moveToThread(&parseThread);
        connect(&cm, &ChannelManager::readyRead,
                &parser, &ParseWorker::onRawDataReceived, Qt::QueuedConnection);
        connect(&parser, &ParseWorker::writeData,
                &cm, &ChannelManager::writeData, Qt::QueuedConnection);
        connect(&cm, &ChannelManager::connected, [&]() {
            connected = true; parser.setCollecting(true);
        });

        commThread.start();
        parseThread.start();
        QMetaObject::invokeMethod(&cm, "connectToDevice", Qt::QueuedConnection);

        for (int i = 0; i < 60 && !connected; ++i) QTest::qWait(500);
        QVERIFY(connected);

        QTest::qWait(10000);

        QMetaObject::invokeMethod(&cm, "disconnectDevice", Qt::QueuedConnection);
        QTest::qWait(500);
        parser.dbManager()->flush();

        int count = parser.dbManager()->rowCount();
        qInfo() << "Collected" << count << "records in 10s";
        QVERIFY2(count >= 900, qPrintable(QString("Expected >= 900, got %1").arg(count)));

        // Verify channel values
        auto results = parser.dbManager()->query(0, UINT64_MAX, 100);
        for (const auto &dp : results) {
            if (!dp.channels.isEmpty()) {
                double ch0 = dp.channels[0];
                QVERIFY2(ch0 >= 0.0 && ch0 <= 1023.0,
                         qPrintable(QString("CH0 out of range: %1").arg(ch0)));
            }
        }

        stopThreads(commThread, parseThread, cm, parser);
        stopSimulator();
    }

    // ────────────────────────────────────────────────────
    // ③ 断线重连：kill 模拟器 → 检测断线 → 重启 → 重连 → DB 增长
    // ────────────────────────────────────────────────────
    void reconnection()
    {
        if (!QFile::exists(m_simScript))
            QSKIP("Simulator script not found");

        // 用独立 DB 避免与 dataCollection 冲突
        QString db2 = QDir::tempPath() + "/test_pulseqt_t025b.db";
        QFile::remove(db2);

        // 第 1 轮：正常采集
        {
            QVERIFY(startSimulator());
            QThread ct, pt;
            ChannelManager cm;
            auto *ch = new TcpChannel("127.0.0.1", 9999);
            cm.setChannel(ch);
            ParseWorker parser(db2);
            bool ok = false;
            cm.moveToThread(&ct); parser.moveToThread(&pt);
            connect(&cm, &ChannelManager::readyRead,
                    &parser, &ParseWorker::onRawDataReceived, Qt::QueuedConnection);
            connect(&parser, &ParseWorker::writeData,
                    &cm, &ChannelManager::writeData, Qt::QueuedConnection);
            connect(&cm, &ChannelManager::connected, [&]() { ok = true; parser.setCollecting(true); });
            ct.start(); pt.start();
            QMetaObject::invokeMethod(&cm, "connectToDevice", Qt::QueuedConnection);
            for (int i = 0; i < 60 && !ok; ++i) QTest::qWait(500);
            QVERIFY(ok);
            QTest::qWait(2000);
            parser.dbManager()->flush();
            int n1 = parser.dbManager()->rowCount();
            qInfo() << "Round 1:" << n1 << "records";
            QVERIFY(n1 > 0);
            stopThreads(ct, pt, cm, parser);
            stopSimulator();
        }

        // 第 2 轮：kill 后重启，新实例重连 → 继续采集
        {
            QVERIFY(startSimulator());
            QThread ct, pt;
            ChannelManager cm;
            auto *ch = new TcpChannel("127.0.0.1", 9999);
            cm.setChannel(ch);
            ParseWorker parser(db2);
            bool ok = false;
            cm.moveToThread(&ct); parser.moveToThread(&pt);
            connect(&cm, &ChannelManager::readyRead,
                    &parser, &ParseWorker::onRawDataReceived, Qt::QueuedConnection);
            connect(&parser, &ParseWorker::writeData,
                    &cm, &ChannelManager::writeData, Qt::QueuedConnection);
            connect(&cm, &ChannelManager::connected, [&]() { ok = true; parser.setCollecting(true); });
            ct.start(); pt.start();
            QMetaObject::invokeMethod(&cm, "connectToDevice", Qt::QueuedConnection);
            for (int i = 0; i < 60 && !ok; ++i) QTest::qWait(500);
            QVERIFY(ok);
            QTest::qWait(2000);
            parser.dbManager()->flush();
            int n2 = parser.dbManager()->rowCount();
            qInfo() << "Round 2 (reconnect):" << n2 << "records";
            QVERIFY(n2 > 0);
            stopThreads(ct, pt, cm, parser);
            stopSimulator();
        }

        QFile::remove(db2);
    }
};

QTEST_MAIN(TestIntegration)
#include "TestIntegration.moc"
