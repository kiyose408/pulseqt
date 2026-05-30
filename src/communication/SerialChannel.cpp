#include "SerialChannel.h"
#include <QDebug>
SerialChannel::SerialChannel(const QString& portName, qint32 baudRate,
                             QSerialPort::DataBits dataBits,
                             QSerialPort::StopBits stopBits,
                             QSerialPort::Parity parity,
                             QObject* parent)
    : IChannel(parent)              //先调基类构造函数
    , m_portName(portName)
    , m_baudRate(baudRate)
    , m_dataBits(dataBits)
    , m_stopBits(stopBits)
    , m_parity(parity)
{}

SerialChannel::~SerialChannel()
{
    close();                //析构时自动关闭
}

bool SerialChannel::open()
{
    if(m_serialPort)
        return m_serialPort->isOpen();  //已经打开过

    m_serialPort= new QSerialPort(m_portName,this);
    m_serialPort->setBaudRate(m_baudRate);
    m_serialPort->setDataBits(m_dataBits);
    m_serialPort->setStopBits(m_stopBits);
    m_serialPort->setParity(m_parity);

    //打开物理串口
    if(!m_serialPort->open(QIODevice::ReadWrite)){
        emit errorOccurred(m_serialPort->errorString());
        return false;
    }

    //信号级联：QSerialPort的信号->转成IChannel的信号
    connect(m_serialPort, &QSerialPort::readyRead,this,[this](){
        QByteArray data = m_serialPort->readAll();
        emit readyRead(data);           //转发给上层
    });

    connect(m_serialPort,&QSerialPort::errorOccurred, this,[this](QSerialPort::SerialPortError err){
        if(err != QSerialPort::NoError)
            emit errorOccurred(m_serialPort->errorString());
    });
    emit connected();
    qInfo() << "SerialChannel opend:" << m_portName << m_baudRate;
    return true;
}

void SerialChannel::close()
{
    if(m_serialPort){
        m_serialPort->close();
        delete m_serialPort;
        m_serialPort = nullptr;
        emit disconnected();
    }
}

bool SerialChannel::isOpen() const
{
    return m_serialPort && m_serialPort->isOpen();
}

qint64 SerialChannel::write(const QByteArray& data)
{
    if(!m_serialPort || !m_serialPort->isOpen())
        return -1;
    return m_serialPort ->write(data);
}

