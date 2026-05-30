#ifndef ICHANNEL_H
#define ICHANNEL_H
#include <QObject>
#include <QByteArray>
#include <QString>
class IChannel : public QObject{
    Q_OBJECT
public:
    explicit IChannel(QObject *parent = nullptr)
        :QObject(parent){}
    virtual ~IChannel() override = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 write(const QByteArray& data) = 0;
signals:
    void readyRead(const QByteArray& data);
    void connected();
    void disconnected();
    void errorOccurred(const QString& errorString);

};

#endif // ICHANNEL_H
