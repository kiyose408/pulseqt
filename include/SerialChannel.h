//==============================================================================
// SerialChannel - 串口通道
//
// 封装 QSerialPort，实现 IChannel 抽象接口。
// 构造时存储配置参数，open() 时才真正创建和打开物理串口，
// 这样同一个对象可以 close 后重新 open 不同参数的串口。
//
// 信号级联：QSerialPort 的内部信号 → lambda → emit IChannel 信号
//         上层代码只连接 IChannel 信号，不感知 QSerialPort 的存在。
//==============================================================================

#ifndef SERIALCHANNEL_H
#define SERIALCHANNEL_H

#include "IChannel.h"
#include <QSerialPort>

class SerialChannel : public IChannel
{
    Q_OBJECT

public:
    // 构造时传入串口配置，不立即打开
    // portName:  如 "COM3" (Win) 或 "/dev/ttyUSB0" (Linux)
    // baudRate:  常用 115200, 9600, 921600
    // 其余参数有默认值，一般不需要改
    explicit SerialChannel(const QString &portName,
                           qint32 baudRate = 115200,
                           QSerialPort::DataBits dataBits = QSerialPort::Data8,
                           QSerialPort::StopBits stopBits = QSerialPort::OneStop,
                           QSerialPort::Parity parity = QSerialPort::NoParity,
                           QObject *parent = nullptr);

    ~SerialChannel() override;   // 析构时自动 close()

    //==========================================================================
    // IChannel 接口实现
    //==========================================================================
    bool open() override;                            // 创建并打开物理串口（同步阻塞）
    void close() override;                           // 关闭 + delete QSerialPort
    bool isOpen() const override;                    // 指针非空 && QSerialPort::isOpen()
    qint64 write(const QByteArray &data) override;   // 发送数据到串口

private:
    QSerialPort *m_serialPort = nullptr;   // 底层串口对象（open() 时创建，close() 时销毁）
    QString m_portName;                    // 串口名
    qint32 m_baudRate;                     // 波特率
    QSerialPort::DataBits m_dataBits;      // 数据位（默认 8）
    QSerialPort::StopBits m_stopBits;      // 停止位（默认 1）
    QSerialPort::Parity m_parity;          // 校验位（默认无校验）
};

#endif // SERIALCHANNEL_H
