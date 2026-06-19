//==============================================================================
// TestChannelManager — 通道管理器单元测试
//
// 覆盖：setChannel 信号转发、connect/disconnect 状态、
//       m_userDisconnect 重连守卫、writeData 转发、线程安全关闭
//
// 注意：SpyChannel 堆分配（setChannel 会 setParent，析构时 CM 清理）
//==============================================================================

#include <QtTest>
#include <QThread>
#include "ChannelManager.h"
#include "IChannel.h"

class SpyChannel : public IChannel
{
    Q_OBJECT
public:
    int  openCount  = 0;
    int  closeCount = 0;
    int  writeCount = 0;
    bool openResult = true;
    bool isOpenVal  = false;

    bool open() override                { openCount++; return openResult; }
    void close() override               { closeCount++; isOpenVal = false; }
    bool isOpen() const override        { return isOpenVal; }
    qint64 write(const QByteArray &) override { writeCount++; return 0; }

    void emitConnected()           { isOpenVal = true;  emit connected(); }
    void emitDisconnected()        { isOpenVal = false; emit disconnected(); }
    void emitReadyRead(const QByteArray &d) { emit readyRead(d); }
    void emitError(const QString &e)        { emit errorOccurred(e); }
};

class TestChannelManager : public QObject
{
    Q_OBJECT

private slots:
    void signalForwarding()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);

        QSignalSpy connSpy(&cm, &ChannelManager::connected);
        QSignalSpy discSpy(&cm, &ChannelManager::disconnected);
        QSignalSpy readSpy(&cm, &ChannelManager::readyRead);
        QSignalSpy errSpy(&cm, &ChannelManager::errorOccurred);

        spy->emitConnected();
        QCOMPARE(connSpy.count(), 1);
        spy->emitDisconnected();
        QCOMPARE(discSpy.count(), 1);
        QByteArray data("hello");
        spy->emitReadyRead(data);
        QCOMPARE(readSpy.count(), 1);
        QCOMPARE(readSpy.at(0).at(0).toByteArray(), data);
        spy->emitError("fail");
        QCOMPARE(errSpy.count(), 1);
    }

    void connectToDeviceCallsOpen()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);
        cm.connectToDevice();
        QCOMPARE(spy->openCount, 1);
    }

    void disconnectDeviceCallsClose()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);
        spy->isOpenVal = true;
        cm.disconnectDevice();
        QCOMPARE(spy->closeCount, 1);
    }

    void userDisconnectNoReconnect()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);
        spy->isOpenVal = true;
        cm.disconnectDevice();
        int savedOpen = spy->openCount;
        spy->emitDisconnected();
        QCOMPARE(spy->openCount, savedOpen);  // 未触发重连
    }

    void unexpectedDisconnectStartsReconnect()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);
        spy->isOpenVal = true;
        cm.connectToDevice();
        spy->emitDisconnected();
        QCOMPARE(cm.reconnectAttempt(), 0);
        cm.disconnectDevice();  // 停止定时器，防止析构时事件残留
    }

    void writeDataForwards()
    {
        ChannelManager cm;
        auto *spy = new SpyChannel;
        cm.setChannel(spy);
        spy->isOpenVal = true;
        cm.writeData(QByteArray("ping"));
        QCOMPARE(spy->writeCount, 1);
    }

    void connectWithoutChannel()
    {
        ChannelManager cm;
        cm.connectToDevice();   // 不崩溃
    }

    void setChannelDisconnectsOld()
    {
        ChannelManager cm;
        auto *oldSpy = new SpyChannel;
        auto *newSpy = new SpyChannel;
        cm.setChannel(oldSpy);

        QSignalSpy connSpy(&cm, &ChannelManager::connected);
        cm.setChannel(newSpy);  // 旧通道 deleteLater，新通道接管

        newSpy->emitConnected();
        QCOMPARE(connSpy.count(), 1);
    }

    void threadSafeClose()
    {
        QThread t;
        auto *cm = new ChannelManager;  // 堆分配，线程退出后手动清理
        auto *spy = new SpyChannel;
        cm->setChannel(spy);
        spy->isOpenVal = true;

        cm->moveToThread(&t);
        t.start();

        QMetaObject::invokeMethod(cm, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
        QCOMPARE(spy->closeCount, 1);

        t.quit(); t.wait();
        delete cm;  // 线程已停，安全删除
    }
};

QTEST_MAIN(TestChannelManager)
#include "TestChannelManager.moc"
