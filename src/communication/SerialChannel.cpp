//==============================================================================
// SerialChannel 实现
//
// 关键设计：
// 1. 构造函数只存参数，open() 才 new QSerialPort —— 支持 close 后换端口再 open
// 2. 信号级联：QSerialPort 的内部事件 → lambda 读取/转换 → emit IChannel 信号
// 3. 析构时自动 close()，防止串口泄漏
// 4. open() 是同步的：返回 true = 已连上，返回 false = 失败
//==============================================================================

#include "SerialChannel.h"
#include <QDebug>

//==============================================================================
// 构造 / 析构
//==============================================================================

SerialChannel::SerialChannel(const QString &portName, qint32 baudRate,
                             QSerialPort::DataBits dataBits,
                             QSerialPort::StopBits stopBits,
                             QSerialPort::Parity parity,
                             QObject *parent)
    : IChannel(parent)            // 先初始化基类 QObject
    , m_portName(portName)        // 存储配置参数（不立即打开）
    , m_baudRate(baudRate)
    , m_dataBits(dataBits)
    , m_stopBits(stopBits)
    , m_parity(parity)
{}

SerialChannel::~SerialChannel()
{
    close();   // 确保析构时释放串口资源
}

//==============================================================================
// IChannel 接口实现
//==============================================================================

bool SerialChannel::open()
{
    // 已打开则直接返回
    if (m_serialPort)
        return m_serialPort->isOpen();

    // 创建 QSerialPort 对象（parent = this，随 SerialChannel 生命周期管理）
    m_serialPort = new QSerialPort(m_portName, this);

    // 应用构造时存储的串口参数
    m_serialPort->setBaudRate(m_baudRate);
    m_serialPort->setDataBits(m_dataBits);
    m_serialPort->setStopBits(m_stopBits);
    m_serialPort->setParity(m_parity);

    // 同步打开物理串口（阻塞，立即返回结果）
    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_serialPort->errorString());
        return false;
    }

    //==========================================================================
    // 信号级联：QSerialPort 事件 → IChannel 信号
    // 为什么要转发？上层（ChannelManager、数据层）只依赖 IChannel 接口，
    // 不直接引用 QSerialPort，换通道类型时无需改上层代码
    //==========================================================================

    // 收到数据 → 读取全部 → 转发给上层
    connect(m_serialPort, &QSerialPort::readyRead, this, [this]() {
        QByteArray data = m_serialPort->readAll();
        emit readyRead(data);
    });

    // 串口错误 → 转发错误信息
    connect(m_serialPort, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError)
            emit errorOccurred(m_serialPort->errorString());
    });

    emit connected();
    qInfo() << "SerialChannel opened:" << m_portName << "@" << m_baudRate;
    return true;
}

void SerialChannel::close()
{
    if (m_serialPort) {
        m_serialPort->close();        // 关闭物理串口
        delete m_serialPort;           // 释放资源
        m_serialPort = nullptr;        // 标记为已关闭
        emit disconnected();
    }
}

bool SerialChannel::isOpen() const
{
    // 指针非空 且 底层串口处于打开状态
    return m_serialPort && m_serialPort->isOpen();
}

qint64 SerialChannel::write(const QByteArray &data)
{
    if (!m_serialPort || !m_serialPort->isOpen())
        return -1;
    return m_serialPort->write(data);
}
