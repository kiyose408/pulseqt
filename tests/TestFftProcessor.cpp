//==============================================================================
// TestFftProcessor — FFT 相位提取单元测试
//
// 覆盖:
//   1. 纯 50Hz 正弦波 — 初相 0° → 输出 ≈ 0°
//   2. 已知初相 45° → 输出 ≈ 45°
//   3. 已知初相 -90° → 输出 ≈ -90°
//   4. TCP 配置 N=1000, fs=1000, 50Hz bin=50
//   5. Serial 配置 N=250, fs=250, 50Hz bin=50
//==============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QtMath>
#include "FftProcessor.h"
#include "MagDataBuffer.h"
#include "MagDataPoint.h"

class TestFftProcessor : public QObject
{
    Q_OBJECT

private:
    // 填充 MagDataBuffer 以便 FftProcessor 消费
    void fillBuffer(MagDataBuffer *buf, int fs, int N, double freq,
                    double phaseDeg, double amplitude = 1.0)
    {
        for (int i = 0; i < N; ++i) {
            double t = static_cast<double>(i) / fs;
            double val = amplitude * std::sin(2.0 * M_PI * freq * t
                                              + qDegreesToRadians(phaseDeg));
            buf->push(MagDataPoint::single(val, i));
        }
    }

private slots:

    //─────────────────────────────────────────────────────────────
    // 1. 纯 50Hz — 初相 0°
    //─────────────────────────────────────────────────────────────
    void testPhaseZero()
    {
        FftProcessor::Config cfg;
        cfg.sampleRate = 1000;
        cfg.fftSize    = 1000;
        cfg.targetFreq = 50.0;

        MagDataBuffer buf(2000);
        fillBuffer(&buf, cfg.sampleRate, cfg.fftSize * 2, 50.0, 0.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QSignalSpy spy(&fft, &FftProcessor::phaseReady);

        // 触发一次 FFT
        fft.onTimer();

        QCOMPARE(spy.count(), 1);
        double phase = spy.at(0).at(0).toDouble();

        // 初相 0° → 输出应接近 0° (允许 ±2° 误差)
        QVERIFY2(qAbs(phase) < 2.0,
                 qPrintable(QString("phase=%1 expected ~0").arg(phase)));
    }

    //─────────────────────────────────────────────────────────────
    // 2. 已知初相 +45°
    //─────────────────────────────────────────────────────────────
    void testPhase45()
    {
        FftProcessor::Config cfg;
        cfg.sampleRate = 1000;
        cfg.fftSize    = 1000;
        cfg.targetFreq = 50.0;

        MagDataBuffer buf(2000);
        fillBuffer(&buf, cfg.sampleRate, cfg.fftSize * 2, 50.0, 45.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QSignalSpy spy(&fft, &FftProcessor::phaseReady);
        fft.onTimer();

        double phase = spy.at(0).at(0).toDouble();
        QVERIFY2(qAbs(phase - 45.0) < 2.0,
                 qPrintable(QString("phase=%1 expected ~45").arg(phase)));
    }

    //─────────────────────────────────────────────────────────────
    // 3. 已知初相 -90°
    //─────────────────────────────────────────────────────────────
    void testPhaseMinus90()
    {
        FftProcessor::Config cfg;
        cfg.sampleRate = 1000;
        cfg.fftSize    = 1000;
        cfg.targetFreq = 50.0;

        MagDataBuffer buf(2000);
        fillBuffer(&buf, cfg.sampleRate, cfg.fftSize * 2, 50.0, -90.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QSignalSpy spy(&fft, &FftProcessor::phaseReady);
        fft.onTimer();

        double phase = spy.at(0).at(0).toDouble();
        QVERIFY2(qAbs(phase + 90.0) < 2.0,
                 qPrintable(QString("phase=%1 expected ~-90").arg(phase)));
    }

    //─────────────────────────────────────────────────────────────
    // 4. 串口配置 N=250, fs=250
    //─────────────────────────────────────────────────────────────
    void testSerialConfig()
    {
        FftProcessor::Config cfg;
        cfg.sampleRate = 250;
        cfg.fftSize    = 250;
        cfg.targetFreq = 50.0;

        // 验证 bin 计算: 50 * 250 / 250 = 50
        MagDataBuffer buf(500);
        fillBuffer(&buf, cfg.sampleRate, cfg.fftSize * 2, 50.0, 30.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QSignalSpy spy(&fft, &FftProcessor::phaseReady);
        fft.onTimer();

        double phase = spy.at(0).at(0).toDouble();
        QVERIFY2(qAbs(phase - 30.0) < 2.0,
                 qPrintable(QString("phase=%1 expected ~30").arg(phase)));
    }

    //─────────────────────────────────────────────────────────────
    // 5. 数据不足时不应 crash
    //─────────────────────────────────────────────────────────────
    void testInsufficientData()
    {
        FftProcessor::Config cfg;
        cfg.sampleRate = 1000;
        cfg.fftSize    = 1000;
        cfg.targetFreq = 50.0;

        MagDataBuffer buf(2000);
        // 只放 100 个样本 (远小于 N=1000)
        fillBuffer(&buf, cfg.sampleRate, 100, 50.0, 0.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QSignalSpy spy(&fft, &FftProcessor::phaseReady);
        fft.onTimer();

        // 数据不足时应跳过，不 emit 也不 crash
        QCOMPARE(spy.count(), 0);
    }

    //─────────────────────────────────────────────────────────────
    // 6. start/stop 状态
    //─────────────────────────────────────────────────────────────
    void testStartStop()
    {
        FftProcessor::Config cfg;
        MagDataBuffer buf(2000);
        fillBuffer(&buf, cfg.sampleRate, cfg.fftSize * 2, 50.0, 0.0);

        FftProcessor fft(cfg, &buf,
            [](const MagDataPoint &p) { return p.data[0]; });

        QVERIFY(!fft.isRunning());
        fft.start();
        QVERIFY(fft.isRunning());
        fft.stop();
        QVERIFY(!fft.isRunning());
    }
};

QTEST_MAIN(TestFftProcessor)
#include "TestFftProcessor.moc"
