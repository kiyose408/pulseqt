#ifndef PARSEWORKER_H
#define PARSEWORKER_H

#include <QObject>
#include <QByteArray>
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

signals:
    void dataPointReady();

private:
    ProtocolDecoder  m_decoder;
    bool m_collecting = false;
    DataBuffer       m_buffer{10000};
    DatabaseManager  m_dbManager;
    //三个成员都是值对象（不是指针 new）
    //，随 ParseWorker 一起 moveToThread，自动归属解析线程。不用手动管理生命周期。
};

#endif // PARSEWORKER_H
