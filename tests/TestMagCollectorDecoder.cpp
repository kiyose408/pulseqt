//==============================================================================
// TestMagCollectorDecoder — MagCollectorDecoder 单元测试
//
// 覆盖：
//   1. 完整单帧解码
//   2. 帧头错误恢复
//   3. 帧尾错误检测
//   4. 校验和错误检测
//   5. 粘包（多帧拼接）
//   6. 1000 帧压力测试
//==============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QByteArray>
#include "MagCollectorDecoder.h"
#include "MagCollectorFrame.h"

// 构建一个合法的 48 字节测试帧
static QByteArray makeTestFrame(uint32_t seq, const int32_t data[6])
{
    QByteArray raw;
    raw.reserve(48);

    // Header
    raw.append(static_cast<char>(0x46));  // 'F'
    raw.append(static_cast<char>(0x4D));  // 'M'

    // Length (uint16 LE = 48)
    raw.append(static_cast<char>(48 & 0xFF));
    raw.append(static_cast<char>((48 >> 8) & 0xFF));

    // Sequence (uint32 LE)
    raw.append(static_cast<char>(seq & 0xFF));
    raw.append(static_cast<char>((seq >> 8) & 0xFF));
    raw.append(static_cast<char>((seq >> 16) & 0xFF));
    raw.append(static_cast<char>((seq >> 24) & 0xFF));

    // Timestamp (12B) — fill with zeros
    for (int i = 0; i < 12; ++i)
        raw.append('\x00');

    // 6 channels (3B int24 + 1B flag)
    static const uint8_t chFlags[6] = {0x58,0x59,0x5A,0x78,0x79,0x7A};
    for (int ch = 0; ch < 6; ++ch) {
        int32_t val = data ? data[ch] : 0;
        if (val < 0) val += 0x1000000;  // int24 无符号
        raw.append(static_cast<char>(val & 0xFF));
        raw.append(static_cast<char>((val >> 8) & 0xFF));
        raw.append(static_cast<char>((val >> 16) & 0xFF));
        raw.append(static_cast<char>(chFlags[ch]));
    }

    // Checksum (uint16 LE) — 覆盖 offset 4~43
    auto *payload = reinterpret_cast<const uint8_t*>(raw.constData() + 4);
    uint16_t cs = MagCollectorFrame::computeChecksum(payload, 40);
    raw.append(static_cast<char>(cs & 0xFF));
    raw.append(static_cast<char>((cs >> 8) & 0xFF));

    // Footer
    raw.append(static_cast<char>(0x0D));  // CR
    raw.append(static_cast<char>(0x0A));  // LF

    return raw;
}


class TestMagCollectorDecoder : public QObject
{
    Q_OBJECT

private slots:

    //─────────────────────────────────────────────────────────────
    // 1. 完整单帧解码
    //─────────────────────────────────────────────────────────────
    void testSingleFrame()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        int32_t data[6] = {100, -200, 300, -400, 500, -600};
        QByteArray raw = makeTestFrame(42, data);
        decoder.feed(raw);

        QCOMPARE(spy.count(), 1);

        auto args = spy.takeFirst();
        auto frame = args.at(0).value<MagCollectorFrame>();

        QCOMPARE(frame.sequence,    42u);
        QCOMPARE(frame.frameLength, 48u);
        QCOMPARE(frame.data[0],     100);
        QCOMPARE(frame.data[1],     -200);
        QCOMPARE(frame.data[2],     300);
        QCOMPARE(frame.data[3],     -400);
        QCOMPARE(frame.data[4],     500);
        QCOMPARE(frame.data[5],     -600);

        QCOMPARE(frame.flags[0], static_cast<uint8_t>(0x58));
        QCOMPARE(frame.flags[1], static_cast<uint8_t>(0x59));
        QCOMPARE(frame.flags[5], static_cast<uint8_t>(0x7A));
    }

    //─────────────────────────────────────────────────────────────
    // 2. 帧头错误恢复 — "FM" 前面有杂字节
    //─────────────────────────────────────────────────────────────
    void testHeaderNoise()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        // 前面塞杂字节
        QByteArray raw;
        raw.append("\x00\x01\x02\x03");   // noise
        raw.append(makeTestFrame(1, nullptr));

        decoder.feed(raw);

        QCOMPARE(spy.count(), 1);
        auto frame = spy.takeFirst().at(0).value<MagCollectorFrame>();
        QCOMPARE(frame.sequence, 1u);
    }

    //─────────────────────────────────────────────────────────────
    // 3. 帧头中间出现 'F' 干扰
    //─────────────────────────────────────────────────────────────
    void testFalseF()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        // 在 'F' 后不是 'M'，状态机应回退并重新找 'F'
        QByteArray raw;
        raw.append(static_cast<char>(0x46));  // 'F'
        raw.append(static_cast<char>(0x00));  // 不是 'M'
        // 然后是一个完整帧
        raw.append(makeTestFrame(99, nullptr));

        // 注意：帧内数据可能含有 0x46，但状态机按长度跳过
        // 喂入后，第一个 'F'+0x00 被丢弃，完整帧被正确解析
        decoder.feed(raw);
        QVERIFY(spy.count() >= 1);
    }

    //─────────────────────────────────────────────────────────────
    // 4. 校验和错误 → 不 emit frameDecoded
    //─────────────────────────────────────────────────────────────
    void testChecksumError()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spyOk(&decoder, &MagCollectorDecoder::frameDecoded);
        QSignalSpy spyErr(&decoder, &MagCollectorDecoder::checksumError);

        QByteArray raw = makeTestFrame(1, nullptr);
        // 篡改 payload 某一字节 (不破坏帧头/帧尾)
        raw[10] = static_cast<char>(raw[10] ^ 0xFF);  // 翻转

        decoder.feed(raw);

        QCOMPARE(spyOk.count(),  0);   // 不应输出
        QCOMPARE(spyErr.count(), 1);   // 应报错
    }

    //─────────────────────────────────────────────────────────────
    // 5. 帧尾错误 → 不 emit
    //─────────────────────────────────────────────────────────────
    void testFooterError()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spyOk(&decoder, &MagCollectorDecoder::frameDecoded);

        QByteArray raw = makeTestFrame(1, nullptr);
        // 帧尾改错
        raw[46] = 0x00;
        raw[47] = 0x00;

        decoder.feed(raw);
        QCOMPARE(spyOk.count(), 0);
    }

    //─────────────────────────────────────────────────────────────
    // 6. 粘包：2 帧拼在一起
    //─────────────────────────────────────────────────────────────
    void testTwoFramesBackToBack()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        QByteArray raw;
        int32_t data1[6] = {1,0,0,0,0,0};
        int32_t data2[6] = {2,0,0,0,0,0};
        raw.append(makeTestFrame(10, data1));
        raw.append(makeTestFrame(20, data2));

        decoder.feed(raw);

        QCOMPARE(spy.count(), 2);
        auto f1 = spy.at(0).at(0).value<MagCollectorFrame>();
        auto f2 = spy.at(1).at(0).value<MagCollectorFrame>();
        QCOMPARE(f1.sequence,  10u);
        QCOMPARE(f2.sequence,  20u);
        QCOMPARE(f1.data[0],   1);
        QCOMPARE(f2.data[0],   2);
    }

    //─────────────────────────────────────────────────────────────
    // 7. 拆包：帧分两次 feed
    //─────────────────────────────────────────────────────────────
    void testSplitFrame()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        QByteArray raw = makeTestFrame(77, nullptr);

        // 前半 20 字节
        decoder.feed(raw.left(20));
        QCOMPARE(spy.count(), 0);  // 还没完

        // 后半
        decoder.feed(raw.mid(20));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<MagCollectorFrame>().sequence, 77u);
    }

    //─────────────────────────────────────────────────────────────
    // 8. 通道标志位错误 → 不 emit
    //─────────────────────────────────────────────────────────────
    void testFlagError()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spyOk(&decoder, &MagCollectorDecoder::frameDecoded);
        QSignalSpy spyErr(&decoder, &MagCollectorDecoder::checksumError);

        QByteArray raw = makeTestFrame(1, nullptr);
        // 篡改 X1 标志位 (offset 23)
        raw[23] = static_cast<char>(0x00);

        // 需要重新算校验和（改动在 payload 区内，需修复）
        auto *p = reinterpret_cast<const uint8_t*>(raw.constData() + 4);
        uint16_t cs = MagCollectorFrame::computeChecksum(p, 40);
        raw[44] = static_cast<char>(cs & 0xFF);
        raw[45] = static_cast<char>((cs >> 8) & 0xFF);

        decoder.feed(raw);
        QCOMPARE(spyOk.count(),  0);
        QCOMPARE(spyErr.count(), 1);
    }

    //─────────────────────────────────────────────────────────────
    // 9. reset() 测试
    //─────────────────────────────────────────────────────────────
    void testReset()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        QByteArray raw = makeTestFrame(1, nullptr);
        decoder.feed(raw.left(20));
        decoder.reset();                                // 中途重置
        decoder.feed(raw);
        QCOMPARE(spy.count(), 1);                       // 只输出 1 帧
    }

    //─────────────────────────────────────────────────────────────
    // 10. 压力测试：1000 帧
    //─────────────────────────────────────────────────────────────
    void testThousandFrames()
    {
        MagCollectorDecoder decoder;
        QSignalSpy spy(&decoder, &MagCollectorDecoder::frameDecoded);

        QByteArray bulk;
        for (int i = 0; i < 1000; ++i) {
            int32_t d[6] = {i, i+1, i+2, i+3, i+4, i+5};
            bulk.append(makeTestFrame(i, d));
        }

        decoder.feed(bulk);
        QCOMPARE(spy.count(), 1000);

        // 抽查第 0 帧、第 500 帧、第 999 帧
        QCOMPARE(spy.at(0).at(0).value<MagCollectorFrame>().sequence,    0u);
        QCOMPARE(spy.at(0).at(0).value<MagCollectorFrame>().data[0],     0);
        QCOMPARE(spy.at(500).at(0).value<MagCollectorFrame>().sequence,  500u);
        QCOMPARE(spy.at(500).at(0).value<MagCollectorFrame>().data[5],   505);
        QCOMPARE(spy.at(999).at(0).value<MagCollectorFrame>().sequence,  999u);
        QCOMPARE(spy.at(999).at(0).value<MagCollectorFrame>().data[0],   999);
    }
};

QTEST_MAIN(TestMagCollectorDecoder)
#include "TestMagCollectorDecoder.moc"
