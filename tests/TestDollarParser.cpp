//==============================================================================
// TestDollarParser + TestMagFormula — 串口解析器 + 磁场公式单元测试
//
// 覆盖:
//   1. MagFormula::fromAdc    — 边界值、零值、精度
//   2. MagFormula::fromSerial — 边界值、零值
//   3. DollarParser 正常解析   — 标准格式
//   4. 多行粘包                — 一次 feed 多行
//   5. 半行缓存                — 无 \n 结尾
//   6. 非 $ 行跳过             — 垃圾行
//   7. 格式不匹配              — $ 开头但格式错
//==============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QByteArray>
#include "DollarParser.h"
#include "MagFormula.h"


class TestDollarParser : public QObject
{
    Q_OBJECT

private slots:

    //══════════════════════════════════════════════════════════
    // MagFormula 测试
    //══════════════════════════════════════════════════════════

    void testFromAdc_zero()
    {
        QCOMPARE(MagFormula::fromAdc(0), 0.0);
    }

    void testFromAdc_max()
    {
        // raw = 8388607 → Mag = 8388607 × 100000 / 8388607 = 100000
        double mag = MagFormula::fromAdc(8388607);
        QVERIFY(qAbs(mag - 100000.0) < 0.001);
    }

    void testFromAdc_half()
    {
        // raw = 4194304 → Mag ≈ 50000
        double mag = MagFormula::fromAdc(4194304);
        QVERIFY(qAbs(mag - 50000.988) < 0.01);  // 近似值
    }

    void testFromAdc_negative()
    {
        double mag = MagFormula::fromAdc(-8388607);
        QVERIFY(qAbs(mag + 100000.0) < 0.001);
    }

    void testFromSerial_zero()
    {
        QCOMPARE(MagFormula::fromSerial(0.0), 0.0);
    }

    void testFromSerial_basic()
    {
        QCOMPARE(MagFormula::fromSerial(3.5), 1.0);
        QCOMPARE(MagFormula::fromSerial(7.0), 2.0);
        QCOMPARE(MagFormula::fromSerial(3500000.0), 1000000.0);
    }

    //══════════════════════════════════════════════════════════
    // DollarParser 测试
    //══════════════════════════════════════════════════════════

    void testParseSingleLine()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        parser.feed(QByteArrayLiteral("$1234567.123456\n"));

        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        double value = args.at(0).toDouble();
        QVERIFY(qAbs(value - 1234567.123456) < 0.000001);
    }

    void testParseSmallValue()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        parser.feed(QByteArrayLiteral("$0000042.000001\n"));

        QCOMPARE(spy.count(), 1);
        double value = spy.at(0).at(0).toDouble();
        QVERIFY(qAbs(value - 42.000001) < 0.000001);
    }

    void testParseZero()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        parser.feed(QByteArrayLiteral("$0000000.000000\n"));

        QCOMPARE(spy.count(), 1);
        double value = spy.at(0).at(0).toDouble();
        QCOMPARE(value, 0.0);
    }

    void testParseMaxValue()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        parser.feed(QByteArrayLiteral("$9999999.999999\n"));

        QCOMPARE(spy.count(), 1);
        double value = spy.at(0).at(0).toDouble();
        QVERIFY(qAbs(value - 9999999.999999) < 0.000001);
    }

    void testMultiLine()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        QByteArray data;
        data.append("$0000001.000000\n");
        data.append("$0000002.000000\n");
        data.append("$0000003.000000\n");
        parser.feed(data);

        QCOMPARE(spy.count(), 3);
        QVERIFY(qAbs(spy.at(0).at(0).toDouble() - 1.0) < 0.000001);
        QVERIFY(qAbs(spy.at(1).at(0).toDouble() - 2.0) < 0.000001);
        QVERIFY(qAbs(spy.at(2).at(0).toDouble() - 3.0) < 0.000001);
    }

    void testPartialLine()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        // 先喂半行（无 \n）
        parser.feed(QByteArrayLiteral("$1234567.123456"));
        QCOMPARE(spy.count(), 0);  // 不应触发

        // 再喂 \n
        parser.feed(QByteArrayLiteral("\n"));
        QCOMPARE(spy.count(), 1);
    }

    void testSplitLine()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        // 一次只发几个字节
        parser.feed(QByteArrayLiteral("$12"));
        parser.feed(QByteArrayLiteral("34567.12"));
        parser.feed(QByteArrayLiteral("3456\n"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(qAbs(spy.at(0).at(0).toDouble() - 1234567.123456) < 0.000001);
    }

    void testNonDollarLines()
    {
        DollarParser parser;
        QSignalSpy spyVal(&parser, &DollarParser::valueReady);

        QByteArray data;
        data.append("garbage line\n");
        data.append("$1234567.000000\n");   // 合法
        data.append("another garbage\n");
        parser.feed(data);

        QCOMPARE(spyVal.count(), 1);  // 只有合法的被解析
    }

    void testDollarFormatMismatch()
    {
        DollarParser parser;
        QSignalSpy spyVal(&parser, &DollarParser::valueReady);
        QSignalSpy spyErr(&parser, &DollarParser::parseError);

        // $ 开头但格式错误
        parser.feed(QByteArrayLiteral("$abc\n"));
        QCOMPARE(spyVal.count(), 0);
        QCOMPARE(spyErr.count(), 1);
    }

    void testTimestamp()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        qint64 before = QDateTime::currentMSecsSinceEpoch();
        parser.feed(QByteArrayLiteral("$0000001.000000\n"));
        qint64 after = QDateTime::currentMSecsSinceEpoch();

        QCOMPARE(spy.count(), 1);
        qint64 ts = spy.at(0).at(1).toLongLong();
        QVERIFY(ts >= before);
        QVERIFY(ts <= after);
    }

    void testCrLf()
    {
        DollarParser parser;
        QSignalSpy spy(&parser, &DollarParser::valueReady);

        // \r\n 结尾
        parser.feed(QByteArrayLiteral("$0000001.000000\r\n"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(qAbs(spy.at(0).at(0).toDouble() - 1.0) < 0.000001);
    }
};

QTEST_MAIN(TestDollarParser)
#include "TestDollarParser.moc"
