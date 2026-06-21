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

// 构建握手请求帧：channelCount(1B) + types(NB)
static QByteArray makeHandshakeFrame(int chCount, const QVector<uint8_t> &types)
{
    QByteArray payload;
    payload.append(static_cast<char>(chCount));
    for (uint8_t t : types)
        payload.append(static_cast<char>(t));
    return makeFrame(Frame::TYPE_HANDSHAKE_REQ, payload);
}

// 构建指定类型的数据帧（用于握手后测试不同类型）
static QByteArray makeTypedDataFrame(const QByteArray &payload)
{
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
    // ⑥ 无效 payload → 不崩溃（未握手回退模式下 <2B 被拒绝）
    // ────────────────────────────────────────────────────
    void invalidPayloadIgnored()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        QSignalSpy spy(&pw, &ParseWorker::dataPointReady);

        // payload 不足 2 字节的数据帧（回退模式最小 1 通道 uint16 = 2B）
        QByteArray shortPayload(1, '\0');
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

    // ═══════════════════════════════════════════════════
    // 握手帧测试
    // ═══════════════════════════════════════════════════

    // ⑧ 握手帧正确解析 3 通道 uint16 → handshakeCompleted 信号
    void handshakeValidUint16()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::handshakeCompleted);
        QSignalSpy ackSpy(&pw, &ParseWorker::writeData);

        QVector<uint8_t> types = {Frame::CH_UINT16, Frame::CH_UINT16, Frame::CH_UINT16};
        pw.onRawDataReceived(makeHandshakeFrame(3, types));
        QTest::qWait(10);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);  // channelCount

        // 验证回复了 ACK=0x00
        QVERIFY(ackSpy.count() >= 1);
        QByteArray ack = ackSpy.at(0).at(0).toByteArray();
        QVERIFY(ack.size() >= 5);
        QCOMPARE(static_cast<uint8_t>(ack[3]), Frame::TYPE_HANDSHAKE_ACK);
        QCOMPARE(static_cast<uint8_t>(ack[4]), uint8_t(0x00));  // OK
    }

    // ⑨ 握手后数据帧按 uint16 正确解析
    void handshakeThenParseUint16()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        // 先握手
        QVector<uint8_t> types = {Frame::CH_UINT16, Frame::CH_UINT16};
        pw.onRawDataReceived(makeHandshakeFrame(2, types));
        QTest::qWait(10);

        // 发送 2 通道数据帧: ch0=0x0102(258), ch1=0x0304(772)
        QByteArray payload(4, '\0');
        payload[0] = '\x02'; payload[1] = '\x01';   // ch0 LE
        payload[2] = '\x04'; payload[3] = '\x03';   // ch1 LE
        pw.onRawDataReceived(makeTypedDataFrame(payload));
        QTest::qWait(10);

        auto snap = pw.buffer()->snapshot();
        QVERIFY(snap.size() >= 1);
        QCOMPARE(snap.last().channels.size(), 2);
        QCOMPARE(snap.last().channels[0], 258.0);
        QCOMPARE(snap.last().channels[1], 772.0);
    }

    // ⑩ 握手后数据帧按 float 正确解析
    void handshakeThenParseFloat()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        QVector<uint8_t> types = {Frame::CH_FLOAT};
        pw.onRawDataReceived(makeHandshakeFrame(1, types));
        QTest::qWait(10);

        // 发送 1 通道 float: 3.14f (0x4048F5C3 LE)
        float val = 3.14f;
        QByteArray payload(4, '\0');
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        payload[0] = bits & 0xFF;
        payload[1] = (bits >> 8) & 0xFF;
        payload[2] = (bits >> 16) & 0xFF;
        payload[3] = (bits >> 24) & 0xFF;
        pw.onRawDataReceived(makeTypedDataFrame(payload));
        QTest::qWait(10);

        auto snap = pw.buffer()->snapshot();
        QVERIFY(snap.size() >= 1);
        QCOMPARE(snap.last().channels.size(), 1);
        QVERIFY(qAbs(snap.last().channels[0] - 3.14) < 0.001);
    }

    // ⑪ 握手 channelCount=0 → 被拒绝
    void handshakeRejectZeroChannels()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::handshakeCompleted);
        QSignalSpy ackSpy(&pw, &ParseWorker::writeData);

        QVector<uint8_t> types;
        pw.onRawDataReceived(makeHandshakeFrame(0, types));
        QTest::qWait(10);

        QCOMPARE(spy.count(), 0);  // 未触发 handshakeCompleted
        QVERIFY(ackSpy.count() >= 1);
        QByteArray ack = ackSpy.at(0).at(0).toByteArray();
        QCOMPARE(static_cast<uint8_t>(ack[3]), Frame::TYPE_HANDSHAKE_ACK);
        QCOMPARE(static_cast<uint8_t>(ack[4]), uint8_t(0x01));  // rejected
    }

    // ⑫ 握手 type=0x09（不支持）→ 被拒绝
    void handshakeRejectUnknownType()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::handshakeCompleted);
        QSignalSpy ackSpy(&pw, &ParseWorker::writeData);

        QVector<uint8_t> types = {0x09};  // 不存在
        pw.onRawDataReceived(makeHandshakeFrame(1, types));
        QTest::qWait(10);

        QCOMPARE(spy.count(), 0);
        QVERIFY(ackSpy.count() >= 1);
        QByteArray ack = ackSpy.at(0).at(0).toByteArray();
        QCOMPARE(static_cast<uint8_t>(ack[4]), uint8_t(0x01));
    }

    // ⑬ channelCount > 16 → 被拒绝
    void handshakeRejectTooManyChannels()
    {
        ParseWorker pw(m_dbPath);
        QSignalSpy spy(&pw, &ParseWorker::handshakeCompleted);

        QVector<uint8_t> types(17, Frame::CH_UINT16);
        pw.onRawDataReceived(makeHandshakeFrame(17, types));
        QTest::qWait(10);

        QCOMPARE(spy.count(), 0);
    }

    // ⑭ resetChannelConfig 后回退到旧行为
    void resetChannelConfigFallback()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        // 先握手
        QVector<uint8_t> types = {Frame::CH_UINT16, Frame::CH_UINT16};
        pw.onRawDataReceived(makeHandshakeFrame(2, types));
        QTest::qWait(10);

        // 重置后应回退到默认
        pw.resetChannelConfig();

        // 发送 6 字节数据（旧行为: 3 通道 uint16）
        QSignalSpy spy(&pw, &ParseWorker::dataPointReady);
        pw.onRawDataReceived(makeDataFrame(10, 20, 30));
        QTest::qWait(10);

        QVERIFY(spy.count() >= 1);
        auto snap = pw.buffer()->snapshot();
        QVERIFY(snap.size() >= 1);
        QCOMPARE(snap.last().channels.size(), 3);
        QCOMPARE(snap.last().channels[0], 10.0);
    }

    // ⑮ 握手前后混合类型(uint8 + int16 + float) 正确解析
    void handshakeMixedTypes()
    {
        ParseWorker pw(m_dbPath);
        pw.setCollecting(true);

        QVector<uint8_t> types = {Frame::CH_UINT8, Frame::CH_INT16, Frame::CH_FLOAT};
        pw.onRawDataReceived(makeHandshakeFrame(3, types));
        QTest::qWait(10);

        // 构建: ch0=200(uint8), ch1=-100(int16), ch2=1.5(float)
        QByteArray payload;
        payload.append('\xC8');                          // uint8 200
        int16_t neg = -100;
        payload.append(static_cast<char>(neg & 0xFF));    // int16 LE low
        payload.append(static_cast<char>((neg >> 8) & 0xFF));  // int16 LE high
        float fv = 1.5f;
        uint32_t bits;
        std::memcpy(&bits, &fv, sizeof(bits));
        payload.append(static_cast<char>(bits & 0xFF));
        payload.append(static_cast<char>((bits >> 8) & 0xFF));
        payload.append(static_cast<char>((bits >> 16) & 0xFF));
        payload.append(static_cast<char>((bits >> 24) & 0xFF));

        pw.onRawDataReceived(makeTypedDataFrame(payload));
        QTest::qWait(10);

        auto snap = pw.buffer()->snapshot();
        QVERIFY(snap.size() >= 1);
        QCOMPARE(snap.last().channels.size(), 3);
        QCOMPARE(snap.last().channels[0], 200.0);
        QCOMPARE(snap.last().channels[1], -100.0);
        QVERIFY(qAbs(snap.last().channels[2] - 1.5) < 0.001);
    }
};

QTEST_MAIN(TestParseWorker)
#include "TestParseWorker.moc"
