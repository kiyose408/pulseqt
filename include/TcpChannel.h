//==============================================================================
// TcpChannel - TCP 客户端通道
//
// 封装 QTcpSocket（客户端模式），实现 IChannel 抽象接口。
//
// 与 SerialChannel 的关键区别：
// 1. open() 是异步的：connectToHost() 立即返回，连接成功靠 connected 信号通知
// 2. isOpen() 用 state() == ConnectedState，不能用 QTcpSocket::isOpen()
// 3. close() 用 abort() 立即断开（不等待缓冲区发送完毕）
// 4. 连接失败时只发 errorOccurred，不发 disconnected（QTcpSocket 的行为）
//==============================================================================

#ifndef TCPCHANNEL_H
#define TCPCHANNEL_H

#include "IChannel.h"
#include <QTcpSocket>

class TcpChannel : public IChannel
{
    Q_OBJECT

public:
    // host:  目标 IP 地址（如 "127.0.0.1"、"192.168.1.100"）
    // port:  目标端口号（如 9999）
    explicit TcpChannel(const QString &host,
                        quint16 port,
                        QObject *parent = nullptr);

    ~TcpChannel() override;

    //==========================================================================
    // IChannel 接口实现
    //==========================================================================
    bool open() override;                            // 异步发起 TCP 连接
    void close() override;                           // abort + delete socket
    bool isOpen() const override;                    // state() == ConnectedState
    qint64 write(const QByteArray &data) override;   // 发送 TCP 数据

private:
    QString m_host;                // 目标 IP
    quint16 m_port;                // 目标端口
    QTcpSocket *m_socket = nullptr; // TCP socket（open() 时创建）
};

#endif // TCPCHANNEL_H
