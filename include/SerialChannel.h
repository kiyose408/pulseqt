#ifndef SERIALCHANNEL_H
#define SERIALCHANNEL_H
#include "IChannel.h"
#include <QSerialPort>
class SerialChannel:public IChannel
{
    Q_OBJECT
public:
    SerialChannel(const QString& portName,
                  qint32 baudRate = 115200,
                  QSerialPort::DataBits dataBits = QSerialPort::Data8,
                  QSerialPort::StopBits stopBits = QSerialPort::OneStop,
                  QSerialPort::Parity parity = QSerialPort::NoParity,
                  QObject* parent = nullptr);
    ~SerialChannel() override;

    //覆写IChannel的纯虚函数
    bool open() override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;

private:
    QSerialPort* m_serialPort = nullptr;
    QString m_portName;
    qint32 m_baudRate;
    QSerialPort::DataBits m_dataBits;
    QSerialPort::StopBits m_stopBits;
    QSerialPort::Parity m_parity;

};

#endif // SERIALCHANNEL_H
