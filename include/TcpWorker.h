//==============================================================================
// LEGACY — 仅 TestIntegration 测试使用
// 生产代码已迁移到 TcpChannel + ChannelManager (IChannel 体系)
//==============================================================================
#ifndef TCPWORKER_H
#define TCPWORKER_H

#include <QObject>
#include <QTcpSocket>

class TcpWorker :public QObject
{
    Q_OBJECT
public:
    TcpWorker(const QString &host,quint16 port,QObject *parent = nullptr);
    ~TcpWorker();

public slots:
    void open();
    void close();
    void write(const QByteArray &data);

signals:
    void rawDataReceived(const QByteArray &data);
    void connected();
    void disconnected();
    void errorOccurred(const QString &err);

private:
    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket = nullptr;
};

#endif // TCPWORKER_H
