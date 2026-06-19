//==============================================================================
// ChannelManager - 通道生命周期管理器
//
// 职责：封装 IChannel 的连接/断开/自动重连逻辑。
//
// 核心机制：
// 1. 指数退避重连：断线后 1s→2s→4s→8s→16s→30s(封顶)，成功后重置
// 2. 区分主动/被动断开：用户点"断开"不重连（m_userDisconnect=true），
//    意外断线才启动重连（m_userDisconnect=false）
// 3. 信号转发：上层只和 ChannelManager 的 connected/disconnected/readyRead
//    等信号交互，不直接碰 IChannel
//
// 调用关系：
//   MainWindow → ChannelManager → IChannel → QSerialPort/QTcpSocket
//==============================================================================

#ifndef CHANNELMANAGER_H
#define CHANNELMANAGER_H

#include <QObject>
#include <QTimer>
#include "IChannel.h"

class ChannelManager : public QObject
{
    Q_OBJECT

public:
    explicit ChannelManager(QObject *parent = nullptr);
    ~ChannelManager() override;

    // 设置/获取底层通道（由外部创建并注入）
    void setChannel(IChannel *channel);
    IChannel *channel() const;

public slots:
    // 发起连接（设置 m_userDisconnect=false，允许断线后自动重连）
    void connectToDevice();

    // 发送数据到通道（供上层如 ParseWorker 调用）
    void writeData(const QByteArray &data);

    // 主动断开（设置 m_userDisconnect=true，不触发重连）
    void disconnectDevice();

public:

    bool isConnected() const;       // 通道是否处于连接状态
    int reconnectAttempt() const;   // 当前重连尝试次数（调试用）

signals:
    // 转发自 IChannel 的信号
    void connected();                    // 连接成功（含重连成功）
    void disconnected();                 // 连接断开
    void readyRead(const QByteArray &data);
    void errorOccurred(const QString &errorString);

private slots:
    void onConnected();                   // 通道连上 → 停重连，重置计数
    void onDisconnected();                // 通道断开 → 判断是否需重连
    void onError(const QString &error);   // 通道错误 → 转发 + 触发重连（TCP 专用）
    void onReadyRead(const QByteArray &data);  // 纯转发
    void onReconnectTimer();              // 定时器到期 → 尝试重连

private:
    void startReconnect();                // 启动重连定时器
    void stopReconnect();                 // 停止重连定时器
    int nextBackoffMs();                  // 计算下一次重连等待时间

    //==========================================================================
    // 成员变量
    //==========================================================================
    IChannel *m_channel = nullptr;
    QTimer   *m_reconnectTimer = nullptr;

    bool m_userDisconnect = false;    // true=用户主动断（不重连），false=意外断（需重连）
    int  m_reconnectAttempt = 0;      // 当前重连尝试次数（成功或主动断开后归零）

    //==========================================================================
    // 重连参数
    //==========================================================================
    static constexpr int BACKOFF_BASE = 1000;   // 基础间隔 1 秒
    static constexpr int BACKOFF_CAP  = 30000;  // 封顶 30 秒
};

#endif // CHANNELMANAGER_H
