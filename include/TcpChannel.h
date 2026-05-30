#ifndef TCPCHANNEL_H
#define TCPCHANNEL_H

#include "IChannel.h"
#include <QTcpSocket>
class TcpChannel:public IChannel
{
    Q_OBJECT

public:

    explicit TcpChannel(const QString& host,
                        quint16 port,
                        QObject *parent = nullptr);
    ~TcpChannel() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray &data) override;
private:
    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket = nullptr;
};

#endif // TCPCHANNEL_H
