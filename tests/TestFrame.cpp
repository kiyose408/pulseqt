//==============================================================================
// TestFrame — Frame 结构体 + CRC16-CCITT 标准向量测试
//
// 验证：
//   1. crc16_ccitt 标准测试向量（"123456789" → 0x29B1）
//   2. Frame 常量正确性
//==============================================================================

#include <QtTest>
#include "Frame.h"

class TestFrame : public QObject
{
    Q_OBJECT

private slots:
    // CRC16-CCITT 标准测试向量
    // 来源：https://en.wikipedia.org/wiki/CRC-16-CCITT
    void crc16_standardVector()
    {
        const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
        uint16_t result = crc16_ccitt(data, sizeof(data));
        QCOMPARE(result, static_cast<uint16_t>(0x29B1));
    }

    // 验证 Frame 常量值（防回归）
    void frameHeaderConstant()
    {
        QCOMPARE(Frame::HEADER, static_cast<uint16_t>(0xA55A));
    }

    void frameTypeConstants()
    {
        QCOMPARE(Frame::TYPE_DATA,      static_cast<uint8_t>(0xE1));
        QCOMPARE(Frame::TYPE_HEARTBEAT, static_cast<uint8_t>(0xE2));
        QCOMPARE(Frame::TYPE_ACK,       static_cast<uint8_t>(0xE3));
        QCOMPARE(Frame::TYPE_ERROR,     static_cast<uint8_t>(0xFF));
    }
};

QTEST_MAIN(TestFrame)
#include "TestFrame.moc"
