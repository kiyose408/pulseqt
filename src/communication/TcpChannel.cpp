#include "TcpChannel.h"
#include <QDebug>
TcpChannel::TcpChannel(const QString &host,
                       quint16 port,
                       QObject *parent)
    :IChannel(parent)
    ,m_host(host)
    ,m_port(port)
{}

TcpChannel::~TcpChannel()
{
    close();
}

bool TcpChannel::open()
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        return true;

    if (!m_socket) {
        m_socket = new QTcpSocket(this);

        // ⬇ 信号连接必须写在 connectToHost 之前！
        connect(m_socket, &QTcpSocket::connected, this, [this]() {
            qInfo() << "TCP connected to" << m_host << m_port;
            emit connected();               // 异步：几毫秒~几秒后才触发
        });

        connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            emit disconnected();
        });

        connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
            QByteArray data = m_socket->readAll();
            emit readyRead(data);
        });

        connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            emit errorOccurred(m_socket->errorString());
        });
    }

    m_socket->connectToHost(m_host, m_port);    // ⬅ 异步，立即返回
    return true;                                // 返回 true 只表示"已发起连接"
}

void TcpChannel::close()
{
    if(m_socket){
        m_socket->abort();          //立即断开，不等待flush

        delete m_socket;
        m_socket = nullptr;
        emit disconnected();
    }
}

bool TcpChannel::isOpen() const
{
    return m_socket &&
           m_socket->state() == QAbstractSocket::ConnectedState;
}

qint64 TcpChannel::write(const QByteArray &data)
{
    if(!m_socket || !m_socket->isOpen())
        return -1;
    return m_socket ->write(data);
}
