#ifndef SERIALWORKER_H
#define SERIALWORKER_H

#include <QObject>
#include <QSerialPort>
class SerialWorker :public QObject
{
    Q_OBJECT
public:
    SerialWorker(const QString &portName, qint32 baudRate,
                 QObject *parent = nullptr);
    ~SerialWorker();
public slots:
    void open();    //在通信线程中执行
    void close();   //在通信线程中执行

signals:
    void rawDataReceived(const QByteArray &data);
    void connected();
    void disconnected();
    void errorOccurred(const QString &err);

private:
    QString m_portName;
    qint32 m_baudRate;
    QSerialPort *m_serial = nullptr;

};

#endif // SERIALWORKER_H
