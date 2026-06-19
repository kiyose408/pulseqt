//==============================================================================
// ChannelManager 实现
//
// 重连时序示例（服务端关闭后）：
//   断线 → wait 1s → 重连失败 → wait 2s → 失败 → wait 4s → ... → wait 30s
//   成功 → 重置 attempt=0 → 下次断线再从 1s 开始
//
// 设计要点：
// 1. QTimer::setSingleShot(true)：每次只触发一次，下次可以换不同间隔
// 2. 指数退避：BACKOFF_BASE * 2^attempt，封顶 BACKOFF_CAP
// 3. TCP 连接失败只发 errorOccurred，因此 onError 中也需要触发重连
// 4. startReconnect() 防重入：定时器已激活时直接返回
//==============================================================================

#include "ChannelManager.h"
#include <QDebug>

//==============================================================================
// 构造 / 析构
//==============================================================================

ChannelManager::ChannelManager(QObject *parent)
    : QObject(parent)
{
    // 创建单次定时器（只触发一次就停，下次重连可以设不同的间隔）
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    // 定时器到期 → 执行重连尝试
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &ChannelManager::onReconnectTimer);
}

ChannelManager::~ChannelManager()
{
    stopReconnect();   // 停掉定时器

    // 切断与 IChannel 的所有信号连接（防止 channel 发信号给已销毁的 manager）
    if (m_channel) {
        m_channel->disconnect(this);
    }
}

//==============================================================================
// 通道管理
//==============================================================================

void ChannelManager::setChannel(IChannel *channel)
{
    // 先断开旧通道的所有信号连接 + 安全销毁
    if (m_channel) {
        m_channel->disconnect(this);
        m_channel->close();
        m_channel->deleteLater();   // 在所属线程安全销毁，防止泄漏
    }

    m_channel = channel;

    if (m_channel) {
        // 设为子对象：moveToThread 时自动跟随，无需手动管理线程归属
        m_channel->setParent(this);

        // 将新通道的四个信号连接到自己的槽（信号级联）
        connect(m_channel, &IChannel::connected,
                this, &ChannelManager::onConnected);
        connect(m_channel, &IChannel::disconnected,
                this, &ChannelManager::onDisconnected);
        connect(m_channel, &IChannel::readyRead,
                this, &ChannelManager::onReadyRead);
        connect(m_channel, &IChannel::errorOccurred,
                this, &ChannelManager::onError);
    }
}

IChannel *ChannelManager::channel() const
{
    return m_channel;
}

//==============================================================================
// 连接 / 断开
//==============================================================================

void ChannelManager::connectToDevice()
{
    if (!m_channel) {
        qWarning() << "ChannelManager: no channel set";
        return;
    }
    if (m_channel->isOpen()) {
        qInfo() << "ChannelManager: already connected";
        return;
    }

    // 重置状态：标记为"非用户主动操作"，断开后可自动重连
    m_userDisconnect = false;
    stopReconnect();            // 清理可能残留的旧定时器
    m_reconnectAttempt = 0;     // 重连计数归零

    qInfo() << "ChannelManager: connecting...";
    m_channel->open();
}

void ChannelManager::writeData(const QByteArray &data)
{
    if (m_channel && m_channel->isOpen())
        m_channel->write(data);
}

void ChannelManager::disconnectDevice()
{
    // 标记为用户主动操作——后续断线不启动重连
    m_userDisconnect = true;
    stopReconnect();

    if (m_channel && m_channel->isOpen()) {
        qInfo() << "ChannelManager: user disconnected";
        m_channel->close();
    }
}

bool ChannelManager::isConnected() const
{
    return m_channel && m_channel->isOpen();
}

int ChannelManager::reconnectAttempt() const
{
    return m_reconnectAttempt;
}

//==============================================================================
// IChannel 信号 → 槽处理
//==============================================================================

void ChannelManager::onConnected()
{
    // 连上了（含重连成功）：停定时器，重置退避
    stopReconnect();
    m_reconnectAttempt = 0;
    qInfo() << "ChannelManager: connected";
    emit connected();
}

void ChannelManager::onDisconnected()
{
    qInfo() << "ChannelManager: disconnected";

    // 意外断线（非用户主动操作）→ 启动指数退避重连
    if (!m_userDisconnect) {
        startReconnect();
    }
    emit disconnected();
}

void ChannelManager::onError(const QString &error)
{
    qWarning() << "ChannelManager error:" << error;
    emit errorOccurred(error);

    // TCP 连接失败时只发 errorOccurred，不发 disconnected
    // 如果处于自动重连模式且通道未打开，手动触发重连
    if (!m_userDisconnect && m_channel && !m_channel->isOpen()) {
        startReconnect();
    }
}

void ChannelManager::onReadyRead(const QByteArray &data)
{
    emit readyRead(data);   // 纯转发，不加额外逻辑
}

//==============================================================================
// 重连逻辑
//==============================================================================

void ChannelManager::onReconnectTimer()
{
    // 重连尝试次数 +1
    m_reconnectAttempt++;
    qInfo() << "ChannelManager: reconnecting... attempt" << m_reconnectAttempt;

    // 重新打开通道
    m_channel->open();
}

void ChannelManager::startReconnect()
{
    // 防重入：通道为空或定时器已在运行则跳过
    if (!m_channel || m_reconnectTimer->isActive())
        return;

    int delay = nextBackoffMs();
    qInfo() << "ChannelManager: reconnect attempt" << m_reconnectAttempt
            << "in" << delay << "ms";

    m_reconnectTimer->start(delay);
}

void ChannelManager::stopReconnect()
{
    m_reconnectTimer->stop();
}

int ChannelManager::nextBackoffMs()
{
    // 公式：1000 × 2^attempt
    // attempt=0 → 1000ms, 1→2000ms, 2→4000ms, 3→8000ms, 4→16000ms
    // attempt=5 → 32000ms → 封顶 30000ms
    int delay = BACKOFF_BASE * (1 << m_reconnectAttempt);
    if (delay > BACKOFF_CAP)
        delay = BACKOFF_CAP;
    return delay;
}
