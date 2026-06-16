//==============================================================================
// TestProtocolDecoder — ProtocolDecoder 状态机单元测试
//
// 覆盖全部 7 个状态 + 粘包/半帧/CRC错误/乱码注入/reset/空帧
//
// 用例清单：
//   ① singleFrame        完整帧解码
//   ② stickyPacket       粘包（两帧拼在一包）
//   ③ halfFrame          半帧续传
//   ④ crcError           CRC 校验失败
//   ⑤ headerInPayload    Payload 中含 0xA5 0x5A 不误判
//   ⑥ randomNoise        乱码注入不崩溃
//   ⑦ resetMidStream     中途 reset 后恢复
//   ⑧ emptyPayload       空 Payload 帧
//==============================================================================

#include <QtTest>
#include <QRandomGenerator>
#include "ProtocolDecoder.h"
#include "Frame.h"

class TestProtocolDecoder : public QObject
{
    Q_OBJECT

private:
    // ── 辅助：构建一帧完整二进制数据（Header + Length + Type + Payload + CRC16） ──
    // CRC 小端序：低字节在前、高字节在后
    static QByteArray buildFrame(uint8_t type, const QByteArray &payload)
    {
        QByteArray frame;
        frame.append(static_cast<char>(0xA5));                    // Header H
        frame.append(static_cast<char>(0x5A));                    // Header L
        frame.append(static_cast<char>(payload.size()));          // Length
        frame.append(static_cast<char>(type));                    // Type
        frame.append(payload);                                    // Payload

        uint16_t crc = crc16_ccitt(
            reinterpret_cast<const uint8_t *>(frame.constData()),
            frame.size());

        frame.append(static_cast<char>(crc & 0xFF));              // CRC 低字节
        frame.append(static_cast<char>((crc >> 8) & 0xFF));       // CRC 高字节
        return frame;
    }

private slots:
    // ────────────────────────────────────────────────────
    // ① 单帧解码：喂入一帧 → frameDecoded 触发一次，type/payload 匹配
    // ────────────────────────────────────────────────────
    void singleFrame()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);
        QSignalSpy errSpy(&decoder, &ProtocolDecoder::crcError);

        QByteArray payload(6, '\x01');
        decoder.feed(buildFrame(0x01, payload));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(errSpy.count(), 0);

        Frame frame = spy.at(0).at(0).value<Frame>();
        QCOMPARE(frame.type, static_cast<uint8_t>(0x01));
        QCOMPARE(frame.payload, payload);
    }

    // ────────────────────────────────────────────────────
    // ② 粘包：两帧拼接在一起 → frameDecoded 触发两次
    // ────────────────────────────────────────────────────
    void stickyPacket()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);

        QByteArray p1(4, '\xAA');
        QByteArray p2(3, '\xBB');
        QByteArray combined = buildFrame(0x01, p1) + buildFrame(0x03, p2);
        decoder.feed(combined);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).value<Frame>().payload, p1);
        QCOMPARE(spy.at(1).at(0).value<Frame>().payload, p2);
    }

    // ────────────────────────────────────────────────────
    // ③ 半帧续传：前半 → 无信号 → 后半 → 完成
    // ────────────────────────────────────────────────────
    void halfFrame()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);

        QByteArray payload(8, '\xCC');
        QByteArray full = buildFrame(0x01, payload);

        // 在 Length + Type + 2 字节 payload 处切开（状态机在 WAIT_PAYLOAD 中途暂停）
        int splitAt = 6;   // 2(header) + 1(len) + 1(type) + 2(payload)
        QByteArray part1 = full.left(splitAt);
        QByteArray part2 = full.mid(splitAt);

        decoder.feed(part1);
        QCOMPARE(spy.count(), 0);   // 还没凑齐

        decoder.feed(part2);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<Frame>().payload, payload);
    }

    // ────────────────────────────────────────────────────
    // ④ CRC 错误：篡改 CRC → crcError 触发，frameDecoded 不触发
    //    验证状态机恢复：再发一帧好的 → frameDecoded 正常
    // ────────────────────────────────────────────────────
    void crcError()
    {
        ProtocolDecoder decoder;
        QSignalSpy goodSpy(&decoder, &ProtocolDecoder::frameDecoded);
        QSignalSpy badSpy(&decoder, &ProtocolDecoder::crcError);

        QByteArray badFrame = buildFrame(0x01, QByteArray(4, '\xDD'));
        // 翻转 CRC 低字节最低位
        badFrame[badFrame.size() - 2] = badFrame[badFrame.size() - 2] ^ 0x01;
        decoder.feed(badFrame);

        QCOMPARE(goodSpy.count(), 0);
        QCOMPARE(badSpy.count(), 1);

        // 状态机应恢复：再发一帧合法帧
        decoder.feed(buildFrame(0x01, QByteArray(3, '\xEE')));
        QCOMPARE(goodSpy.count(), 1);
    }

    // ────────────────────────────────────────────────────
    // ⑤ Payload 中含 0xA5 0x5A 不误判为帧头
    //    长度字段正确引导跳过 → frameDecoded 只触发一次
    // ────────────────────────────────────────────────────
    void headerInPayload()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);

        // payload 中嵌入 0xA5 0x5A
        QByteArray payload;
        payload.append('\x00');
        payload.append(static_cast<char>(0xA5));
        payload.append(static_cast<char>(0x5A));
        payload.append('\xFF');

        decoder.feed(buildFrame(0x01, payload));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<Frame>().payload, payload);
    }

    // ────────────────────────────────────────────────────
    // ⑥ 乱码注入：1000 字节随机数据 → 不崩溃 → 之后正常帧仍可解码
    // ────────────────────────────────────────────────────
    void randomNoise()
    {
        ProtocolDecoder decoder;
        QSignalSpy goodSpy(&decoder, &ProtocolDecoder::frameDecoded);

        // 注入 1000 字节随机噪声
        QByteArray noise(1000, '\0');
        QRandomGenerator *rng = QRandomGenerator::global();
        for (int i = 0; i < noise.size(); ++i)
            noise[i] = static_cast<char>(rng->bounded(256));
        decoder.feed(noise);

        // 之后发送合法帧，应正常解码
        decoder.feed(buildFrame(0x01, QByteArray(5, '\xFF')));
        QCOMPARE(goodSpy.count(), 1);
        QCOMPARE(goodSpy.at(0).at(0).value<Frame>().type, static_cast<uint8_t>(0x01));
    }

    // ────────────────────────────────────────────────────
    // ⑦ 中途 reset：喂半帧 → reset → 合法帧正确解码
    // ────────────────────────────────────────────────────
    void resetMidStream()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);

        QByteArray full = buildFrame(0x01, QByteArray(5, '\x77'));
        QByteArray half = full.left(5);     // 半帧
        decoder.feed(half);
        QCOMPARE(spy.count(), 0);

        decoder.reset();

        decoder.feed(buildFrame(0x03, QByteArray(3, '\x88')));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<Frame>().type, static_cast<uint8_t>(0x03));
    }

    // ────────────────────────────────────────────────────
    // ⑧ 空 Payload 帧：type=0x02 心跳帧，payload 为空
    // ────────────────────────────────────────────────────
    void emptyPayload()
    {
        ProtocolDecoder decoder;
        QSignalSpy spy(&decoder, &ProtocolDecoder::frameDecoded);

        decoder.feed(buildFrame(0x02, QByteArray()));

        QCOMPARE(spy.count(), 1);
        Frame frame = spy.at(0).at(0).value<Frame>();
        QCOMPARE(frame.type, static_cast<uint8_t>(0x02));
        QVERIFY(frame.payload.isEmpty());
    }
};

QTEST_MAIN(TestProtocolDecoder)
#include "TestProtocolDecoder.moc"
