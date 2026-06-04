#include "TcpWorker.h"
#include <QDebug>


TcpWorker::TcpWorker(const QString &host, quint16 port, QObject *parent)
    :QObject(parent),m_host(host),m_port(port)
{
}

TcpWorker::~TcpWorker() { close(); }

void TcpWorker::open()
{
    m_socket = new QTcpSocket;

    //信号连接在connectToHost之前
    connect(m_socket, &QTcpSocket::connected,this,[this](){
        qInfo() << "TcpWorker: connected to " << m_host <<m_port;
        emit connected();
    });

    connect(m_socket, &QTcpSocket::disconnected,this,[this](){
        emit disconnected();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
        emit rawDataReceived(m_socket->readAll());
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit errorOccurred(m_socket->errorString());
            });

    m_socket->connectToHost(m_host, m_port);
}
void TcpWorker::write(const QByteArray &data)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(data);
}

void TcpWorker::close()
{
    if (m_socket) {
        m_socket->abort();
        delete m_socket;
        m_socket = nullptr;
        emit disconnected();
    }
}
