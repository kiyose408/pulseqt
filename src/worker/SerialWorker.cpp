#include "SerialWorker.h"
#include <QDebug>
SerialWorker::SerialWorker(const QString &portName, qint32 baudRate,
                           QObject *parent)
    :QObject(parent)
    , m_portName(portName)
    ,m_baudRate(baudRate)
{
    //不new QSerialPort  -- 要等moveToThread之后在通信线程里创建
}

SerialWorker::~SerialWorker()
{
    close();
}

void SerialWorker::open()
{
    //创建QSerialPort(此时worker已经被moveToThread到通信线程中)
    m_serial= new QSerialPort(m_portName);
    //设置参数
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setParity(QSerialPort::NoParity);

    //打开物理串口
    if(!m_serial->open(QIODevice::ReadWrite)){
        emit errorOccurred(m_serial->errorString());
        return;
    }

    //连接信号--收到数据--读出来--emit
    connect(m_serial, &QSerialPort::readyRead, this,[this](){
        emit rawDataReceived(m_serial->readAll());
    });

    //错误信号转发
    connect(m_serial, &QSerialPort::errorOccurred,this,
            [this](QSerialPort::SerialPortError err){
                if(err!= QSerialPort::NoError)
            emit errorOccurred(m_serial->errorString());
    });
    emit connected();
    qInfo() << "SerialWorker: opened" << m_portName << "@" <<m_baudRate;
}

void SerialWorker::close()
{
    if(m_serial){
        m_serial ->close();
        delete m_serial;
        m_serial = nullptr;
        emit disconnected();
    }
}
