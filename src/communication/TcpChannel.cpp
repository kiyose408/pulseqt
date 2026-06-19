//==============================================================================
// TcpChannel 实现
//
// 关键设计：
// 1. open() 异步：connectToHost() 立即返回，connected 信号才表示成功
// 2. 信号连接必须在 connectToHost 之前 —— 防止极端情况下信号丢失
// 3. 连接失败只发 errorOccurred，不发 disconnected（QTcpSocket 行为），
//    ChannelManager 的 onError 负责检测并触发重连
// 4. close() 用 abort() 而非 disconnectFromHost()：
//    abort() 立即断开，适合采集场景（不需要等缓冲区排空）
//==============================================================================

#include "TcpChannel.h"
#include <QDebug>

//==============================================================================
// 构造 / 析构
//==============================================================================

TcpChannel::TcpChannel(const QString &host,
                       quint16 port,
                       QObject *parent)
    : IChannel(parent)
    , m_host(host)
    , m_port(port)
{}

TcpChannel::~TcpChannel()
{
    close();
}

//==============================================================================
// IChannel 接口实现
//==============================================================================

bool TcpChannel::open()
{
    // 已连接则直接返回（避免重复连接）
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        return true;

    // 首次调用 open() 时创建 socket 并绑定信号
    if (!m_socket) {
        m_socket = new QTcpSocket(this);

        // ⚠️ 信号连接必须在 connectToHost() 之前！
        // 原因：connectToHost 是异步的，如果先调 connectToHost 再 connect 信号，
        // 极端情况下连接事件可能在信号绑定之前就触发了（信号丢失）。

        connect(m_socket, &QTcpSocket::connected, this, [this]() {
            qInfo() << "TCP connected to" << m_host << m_port;
            emit connected();
        });

        // 断开信号：正常断开或连接失败都可能触发
        connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            emit disconnected();
        });

        // 收到 TCP 数据 → 读取全部 → 转发给上层
        connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
            QByteArray data = m_socket->readAll();
            emit readyRead(data);
        });

        // 异步错误通知（连接被拒、超时、对方关闭等）
        connect(m_socket, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
            emit errorOccurred(m_socket->errorString());
        });
    }

    // 异步发起连接（立即返回，不等待结果）
    m_socket->connectToHost(m_host, m_port);
    return true;   // true 仅表示"请求已发出"，不代表已连上
}

void TcpChannel::close()
{
    if (m_socket) {
        // abort()：立即断开，不等待 TCP 缓冲区排空
        // 对比 disconnectFromHost()：温和断开，等缓冲数据发完再关
        m_socket->abort();

        delete m_socket;
        m_socket = nullptr;
        emit disconnected();
    }
}

bool TcpChannel::isOpen() const
{
    // ⚠️ 不能用 QTcpSocket::isOpen() —— 那个在 ConnectingState 也返回 true
    // 必须用 state() == ConnectedState 精确判断
    return m_socket &&
           m_socket->state() == QAbstractSocket::ConnectedState;
}

qint64 TcpChannel::write(const QByteArray &data)
{
    if (!m_socket || !m_socket->isOpen()) {
        emit errorOccurred("TCP socket not open");
        return -1;
    }
    qint64 n = m_socket->write(data);
    if (n < 0) emit errorOccurred(m_socket->errorString());
    return n;
}

// ── 自注册到全局通道注册表 ──────────────────────────
#include "ChannelRegistry.h"
static const bool tcpRegistered = []() {
    ChannelDescriptor desc;
    desc.id   = "tcp";
    desc.name = "TCP 客户端";
    desc.configFields = {
        ConfigField("host", "主机地址", "string", "127.0.0.1"),
        ConfigField("port", "端口",     "int",    9999),
    };
    desc.factory = +[](const QVariantMap &cfg, QObject *parent) -> IChannel* {
        return new TcpChannel(
            cfg.value("host", "127.0.0.1").toString(),
            cfg.value("port", 9999).toUInt(), parent);
    };
    ChannelRegistry::registerChannel(desc);
    return true;
}();
