//==============================================================================
// ThresholdAlarm 单元测试
//==============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "ThresholdAlarm.h"

class TestThresholdAlarm : public QObject
{
    Q_OBJECT

private slots:
    void upperLimit_trigger()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 100);
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {90};   alarm.process(dp);  // 正常
        QCOMPARE(spy.count(), 0);

        dp.channels = {105};  alarm.process(dp);  // 超上限
        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        QCOMPARE(args[0].toInt(), 0);       // channel
        QCOMPARE(args[1].toDouble(), 105.0); // value
        QCOMPARE(args[2].toDouble(), 100.0); // threshold
        QCOMPARE(args[3].toBool(), true);    // isUpper
    }

    void upperLimit_noRepeat()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 100);
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {110};  alarm.process(dp);  // 第一次触发
        dp.channels = {120};  alarm.process(dp);  // 已在告警，不重复
        QCOMPARE(spy.count(), 1);
    }

    void hysteresis_clear()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 100);
        alarm.setHysteresis(5);
        QSignalSpy trig(&alarm, &ThresholdAlarm::alarmTriggered);
        QSignalSpy cleared(&alarm, &ThresholdAlarm::alarmCleared);

        DataPoint dp;
        dp.channels = {110};  alarm.process(dp);  // 触发
        dp.channels = {98};   alarm.process(dp);  // 未穿越滞回带
        QCOMPARE(cleared.count(), 0);

        dp.channels = {94};   alarm.process(dp);  // 穿越滞回带
        QCOMPARE(cleared.count(), 1);
        QCOMPARE(cleared.takeFirst()[0].toInt(), 0);
    }

    void hysteresis_noFlapping()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 100);
        alarm.setHysteresis(5);
        QSignalSpy trig(&alarm, &ThresholdAlarm::alarmTriggered);
        QSignalSpy cleared(&alarm, &ThresholdAlarm::alarmCleared);

        DataPoint dp;
        // 阈值边界反复穿越 → 应该只触发1次
        QList<double> vals = {99, 101, 99, 101, 99, 101, 99, 101};
        for (double v : vals) {
            dp.channels = {v};
            alarm.process(dp);
        }
        QCOMPARE(trig.count(), 1);       // 只触发1次
        QCOMPARE(cleared.count(), 0);    // 从未清除（因为从未低于95）
    }

    void lowerLimit_trigger()
    {
        ThresholdAlarm alarm;
        alarm.setLowerLimit(0, 50);
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {60};   alarm.process(dp);
        QCOMPARE(spy.count(), 0);

        dp.channels = {40};   alarm.process(dp);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst()[3].toBool(), false);  // isUpper=false
    }

    void multiChannel()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 200);
        alarm.setLowerLimit(1, 100);
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {250, 50};  alarm.process(dp);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy[0][0].toInt(), 0);  // CH0 超上限
        QCOMPARE(spy[1][0].toInt(), 1);  // CH1 低下限
    }

    void channelMask_respected()
    {
        ThresholdAlarm alarm;
        alarm.setUpperLimit(0, 100);
        alarm.setUpperLimit(1, 100);
        alarm.setChannels({1});  // 只对 CH1 生效
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {150, 150};  alarm.process(dp);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst()[0].toInt(), 1);  // 只有 CH1 触发
    }

    void disabled_passthrough()
    {
        ThresholdAlarm alarm;
        alarm.setEnabled(false);
        alarm.setUpperLimit(0, 50);
        QSignalSpy spy(&alarm, &ThresholdAlarm::alarmTriggered);

        DataPoint dp;
        dp.channels = {100};  alarm.process(dp);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestThresholdAlarm)
#include "TestThresholdAlarm.moc"
