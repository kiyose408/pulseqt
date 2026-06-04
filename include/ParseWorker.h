#ifndef PARSEWORKER_H
#define PARSEWORKER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QDateTime>
#include "ProtocolDecoder.h"
#include "DataBuffer.h"
#include "DatabaseManager.h"

class ParseWorker : public QObject
{
    Q_OBJECT
public:
    explicit ParseWorker(QObject *parent = nullptr);
    ~ParseWorker();
    DataBuffer *buffer();
    DatabaseManager *dbManager();

public slots:
    void onRawDataReceived(const QByteArray &data);
    void setCollecting(bool on);
    void onHeartbeatCheck();

signals:
    void dataPointReady();
    void writeData(const QByteArray &data);

private:
    QByteArray buildFrame(uint8_t type, const QByteArray &payload = {});
    ProtocolDecoder  m_decoder;
    bool m_collecting = false;
    QTimer *m_heartbeatTimer = nullptr;
    qint64  m_lastDataTime   = 0;
    int     m_heartbeatMissed = 0;
    DataBuffer       m_buffer{10000};
    DatabaseManager  m_dbManager;
    //三个成员都是值对象（不是指针 new）
    //，随 ParseWorker 一起 moveToThread，自动归属解析线程。不用手动管理生命周期。
};

#endif // PARSEWORKER_H
