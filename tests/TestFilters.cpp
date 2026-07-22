//==============================================================================
// MovingAverageFilter + MedianFilter 单元测试
//==============================================================================

#include <QtTest>
#include "MovingAverageFilter.h"
#include "MedianFilter.h"

class TestFilters : public QObject
{
    Q_OBJECT

private slots:
    // ── MovingAverageFilter — Simple ───────────────────

    void maSimple_window3()
    {
        MovingAverageFilter f(3, MovingAverageFilter::Simple);
        DataPoint dp;

        dp.channels = {1.0};    f.process(dp);              // [1]     avg=1
        dp.channels = {2.0};    f.process(dp);              // [1,2]   avg=1.5
        dp.channels = {3.0};    auto r = f.process(dp);     // [1,2,3] avg=2
        QCOMPARE(r.channels[0], 2.0);

        dp.channels = {4.0};    r = f.process(dp);          // [2,3,4] avg=3
        QCOMPARE(r.channels[0], 3.0);

        dp.channels = {7.0};    r = f.process(dp);          // [3,4,7] avg=14/3
        QVERIFY(qAbs(r.channels[0] - 14.0/3.0) < 0.0001);
    }

    void maSimple_multiChannel()
    {
        MovingAverageFilter f(3, MovingAverageFilter::Simple);
        DataPoint dp;
        dp.channels = {10.0, 100.0};    f.process(dp);
        dp.channels = {20.0, 200.0};    f.process(dp);
        dp.channels = {30.0, 300.0};    auto r = f.process(dp);

        QCOMPARE(r.channels[0], 20.0);  // (10+20+30)/3
        QCOMPARE(r.channels[1], 200.0); // (100+200+300)/3
    }

    // ── MovingAverageFilter — EMA ──────────────────────

    void maEMA_basic()
    {
        MovingAverageFilter f(3, MovingAverageFilter::EMA);
        // alpha = 2/(3+1) = 0.5
        DataPoint dp;

        dp.channels = {100.0};  auto r = f.process(dp);  // 首帧初始化
        QCOMPARE(r.channels[0], 100.0);

        dp.channels = {200.0};  r = f.process(dp);       // 0.5*200 + 0.5*100 = 150
        QCOMPARE(r.channels[0], 150.0);

        dp.channels = {200.0};  r = f.process(dp);       // 0.5*200 + 0.5*150 = 175
        QCOMPARE(r.channels[0], 175.0);
    }

    // ── MedianFilter ───────────────────────────────────

    void median_oddWindow()
    {
        MedianFilter f(3);
        DataPoint dp;

        dp.channels = {1.0};    f.process(dp);               // [1]        med=1
        dp.channels = {3.0};    f.process(dp);               // [1,3]      med=1
        dp.channels = {2.0};    auto r = f.process(dp);      // [1,3,2]    med=2
        QCOMPARE(r.channels[0], 2.0);

        dp.channels = {9.0};    r = f.process(dp);           // [3,2,9]    med=3
        QCOMPARE(r.channels[0], 3.0);
    }

    void median_pulseRejection()
    {
        MedianFilter f(5);
        DataPoint dp;

        // 前 4 帧正常值 ~10
        dp.channels = {10.0}; f.process(dp);
        dp.channels = {10.0}; f.process(dp);
        dp.channels = {10.0}; f.process(dp);
        dp.channels = {10.0}; f.process(dp);

        // 第 5 帧注入脉冲野值 999
        dp.channels = {999.0}; auto r = f.process(dp);
        // 窗口 [10,10,10,10,999] 排序后中位数 = 10
        QCOMPARE(r.channels[0], 10.0);
    }

    void median_multiChannel()
    {
        MedianFilter f(3);
        DataPoint dp;

        dp.channels = {1.0, 10.0};  f.process(dp);
        dp.channels = {5.0, 50.0};  f.process(dp);
        dp.channels = {3.0, 30.0};  auto r = f.process(dp);
        // CH0: [1,5,3] → 排序 [1,3,5] → 中位数 3
        // CH1: [10,50,30] → 排序 [10,30,50] → 中位数 30
        QCOMPARE(r.channels[0], 3.0);
        QCOMPARE(r.channels[1], 30.0);
    }

    // ── 过滤器禁用测试 ────────────────────────────────

    void disabledFilter_passthrough()
    {
        MovingAverageFilter f(3);
        f.setEnabled(false);
        DataPoint dp;
        dp.channels = {42.0};
        auto r = f.process(dp);
        QCOMPARE(r.channels[0], 42.0);  // 禁用时原样返回
    }

    // ── name() 测试 ───────────────────────────────────

    void filterNames()
    {
        MovingAverageFilter ma(5, MovingAverageFilter::Simple);
        QCOMPARE(ma.name(), QString("MovingAverage(5, Simple)"));

        MovingAverageFilter ema(7, MovingAverageFilter::EMA);
        QCOMPARE(ema.name(), QString("MovingAverage(7, EMA)"));

        MedianFilter mf(3);
        QCOMPARE(mf.name(), QString("Median(3)"));
    }
};

QTEST_MAIN(TestFilters)
#include "TestFilters.moc"
