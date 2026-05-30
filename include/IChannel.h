//==============================================================================
// IChannel - 通道抽象接口
//
// 设计目的：让上层业务代码（协议解码、数据管理）不感知底层通道类型，
//         串口和 TCP 通过统一接口访问，方便扩展新通道类型（如蓝牙、UDP）。
//
// 使用方式：所有通道操作都通过 IChannel 指针进行，
//          ChannelManager 持有 IChannel* 统一管理连接生命周期。
//==============================================================================

#ifndef ICHANNEL_H
#define ICHANNEL_H

#include <QObject>
#include <QByteArray>
#include <QString>

class IChannel : public QObject
{
    Q_OBJECT   // 必须：抽象基类也可以声明信号，需要 MOC 生成元对象代码

public:
    explicit IChannel(QObject *parent = nullptr)
        : QObject(parent) {}

    virtual ~IChannel() override = default;

    //==========================================================================
    // 纯虚接口（子类必须覆写）
    //==========================================================================

    // 打开通道
    // 串口：同步阻塞，返回 true 表示已连上
    // TCP：  异步发起，返回 true 仅表示已发起连接请求（connected 信号才确认）
    virtual bool open() = 0;

    // 关闭通道（串口 close，TCP abort）
    virtual void close() = 0;

    // 通道是否处于打开/连接状态
    // 串口：QSerialPort::isOpen()
    // TCP：  state() == ConnectedState（不能用 isOpen()）
    virtual bool isOpen() const = 0;

    // 发送原始字节数据到设备
    // 返回实际发送的字节数，失败返回 -1
    virtual qint64 write(const QByteArray &data) = 0;

    //==========================================================================
    // 信号 — 所有通道统一的事件通知
    //==========================================================================

signals:
    // 收到新数据（已从底层缓冲区读出，直接可用）
    void readyRead(const QByteArray &data);

    // 连接建立成功
    void connected();

    // 连接断开（主动关闭 / 被动断线 / 连接失败）
    void disconnected();

    // 发生错误（打开失败、读写错误、对方关闭连接等）
    void errorOccurred(const QString &errorString);
};

#endif // ICHANNEL_H
