//==============================================================================
// TestParseWorker — ParseWorker 单元测试
//
// 覆盖：数据帧解析、心跳逻辑、setCollecting 切换、心跳应答
//==============================================================================

#include <QtTest>
#include <QDir>
#include <QFile>
#include "ParseWorker.h"
#include "Frame.h"
#include "ProtocolDecoder.h"

// ── 辅助：构建合法协议帧（与 ProtocolDecoder 兼容） ────
extern uint16_t crc16_ccitt(const uint8_t *data, size_t len);

static QByteArray makeFrame(uint8_t type, const QByteArray &payload)
{
    QByteArray raw;
    raw.append('\xA5'); raw.append('\x5A');
    raw.append(static_cast<char>(payload.size()));
    raw.append(static_cast<char>(type));
    raw.append(payload);
    uint16_t crc = crc16_ccitt(
        reinterpret_cast<const uint8_t*>(raw.constData()), raw.size());
    raw.append(static_cast<char>(crc & 0xFF));
    raw.append(static_cast<char>((crc >> 8) & 0xFF));
    return raw;
}

// 构建 3 通道数据帧（每通道 uint16 LE）
static QByteArray makeDataFrame(uint16_t ch0, uint16_t ch1, uint16_t ch2)
{
    QByteArray payload(6, '\0');
    payload[0] = ch0 & 0xFF; payload[1] = (ch0 >> 8) & 0xFF;
    payload[2] = ch1 & 0xFF; payload[3] = (ch1 >> 8) & 0xFF;
    payload[4] = ch2 & 0xFF; payload[5] = (ch2 >> 8) & 0xFF;
    return makeFrame(Frame::TYPE_DATA, payload);
}

class TestParseWorker : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;

private slots:
    void initTestCase()
    {
        m_dbPath = QDir::tempPath() + "/test_parseworker.db";
    }

    void cleanup()
    {
        QFile::remove(m_dbPath);
    }

    // ────────────────────────────────────────────────────
    // ① onRawDataReceived 更新 m_lastDataTime + 重置心跳
    // ────────────────────────────────────────────────────
    void rawDataResetsHeartbeat()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        QSignalSpy hbSpy(&pw, &ParseWorker::writeData);

        // 模拟空闲 → 手动触发心跳检查
        QTest::qWait(10);
        pw.onHeartbeatCheck();   // 刚收到数据，不触发心跳
        QCOMPARE(hbSpy.count(), 0);

        pw.onRawDataReceived(QByteArray(1, '\x00'));
        pw.onHeartbeatCheck();
        QCOMPARE(hbSpy.count(), 0);  // 仍在 5s 内
    }

    // ────────────────────────────────────────────────────
    // ② setCollecting 开关
    // ────────────────────────────────────────────────────
    void setCollectingToggles()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::dataPointReady);

        pw.setCollecting(true);
        pw.onRawDataReceived(makeDataFrame(100, 200, 300));
        QTest::qWait(10);
        QVERIFY(spy.count() >= 1);   // 收集到的数据帧触发了信号

        pw.setCollecting(false);
        int before = spy.count();
        pw.onRawDataReceived(makeDataFrame(400, 500, 600));
        QTest::qWait(10);
        QCOMPARE(spy.count(), before);  // 停止采集后不再触发
    }

    // ────────────────────────────────────────────────────
    // ③ 数据帧 → DataBuffer 正确填充
    // ────────────────────────────────────────────────────
    void dataFramePopulatesBuffer()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        pw.onRawDataReceived(makeDataFrame(100, 200, 300));
        pw.onRawDataReceived(makeDataFrame(400, 500, 600));
        QTest::qWait(10);

        auto snap = pw.buffer()->snapshot();
        QVERIFY(snap.size() >= 2);

        // 最后一个数据点的通道值
        auto last = snap.last();
        QVERIFY(last.channels.size() >= 3);
        QCOMPARE(last.channels[0], 400.0);
        QCOMPARE(last.channels[1], 500.0);
        QCOMPARE(last.channels[2], 600.0);
    }

    // ────────────────────────────────────────────────────
    // ④ 心跳请求帧 → 自动回复 ACK
    // ────────────────────────────────────────────────────
    void heartbeatRequestAutoReplies()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::writeData);

        pw.onRawDataReceived(makeFrame(Frame::TYPE_HEARTBEAT, {}));
        QTest::qWait(10);
        QCOMPARE(spy.count(), 1);  // 发了应答

        // 验证应答帧类型
        QByteArray reply = spy.at(0).at(0).toByteArray();
        QVERIFY(reply.size() >= 4);
        QCOMPARE(reply[0], '\xA5');
        QCOMPARE(reply[1], '\x5A');
        uint8_t fType = static_cast<uint8_t>(reply[3]);
        QCOMPARE(fType, Frame::TYPE_ACK);
    }

    // ────────────────────────────────────────────────────
    // ⑤ 心跳超时 → writeData 发送心跳 → 6 次后停定时器
    // ────────────────────────────────────────────────────
    void heartbeatTimeout()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::writeData);

        // 模拟长时间无数据（手动设 m_lastDataTime 为过去）
        // 通过 onHeartbeatCheck 直接测试
        pw.onHeartbeatCheck();
        QCOMPARE(spy.count(), 0);  // 刚构造，lastDataTime 是现在

        // 无法直接设置 private 成员，测试实际行为：
        // 验证定时器在运行，heartbeatMissed=0 时不会停
        // （此用例以集成方式验证心跳时序，单元测试仅验证方法可调用）
    }

    // ────────────────────────────────────────────────────
    // ⑥ 空 payload / 无效 payload → 不崩溃
    // ────────────────────────────────────────────────────
    void invalidPayloadIgnored()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        QSignalSpy spy(&pw, &ParseWorker::dataPointReady);

        // payload 不足 6 字节的数据帧
        QByteArray shortPayload(3, '\x00');
        pw.onRawDataReceived(makeFrame(Frame::TYPE_DATA, shortPayload));
        QTest::qWait(10);
        QCOMPARE(spy.count(), 0);  // 被忽略
    }

    // ────────────────────────────────────────────────────
    // ⑦ buffer() / dbManager() 返回非空
    // ────────────────────────────────────────────────────
    void accessorsReturnValid()
    {
        ParseWorker pw(m_dbPath);
        QVERIFY(pw.buffer() != nullptr);
        QVERIFY(pw.dbManager() != nullptr);
    }
};

QTEST_MAIN(TestParseWorker)
#include "TestParseWorker.moc"
