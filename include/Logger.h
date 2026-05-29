#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QtGlobal>
#include <QString>
class Logger
{
public:
    static Logger& instance();      //单例入口


    void init(const QString& filePath = "app.log");
    void setMaxFileSize(qint64 bytes);

private:
    Logger() = default;             //构造函数私有
    ~Logger();
    Logger(const Logger&) = delete; //禁止拷贝
    Logger& operator = (const Logger&) = delete;

    //关键：static消息处理器
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext& ctx,
                               const QString& msg);

    void writeLine(const QString& line);        //非static写文件
    void rotateIfNeeded();                      //非static轮转检查

    QFile       m_file;
    qint64      m_maxFileSize = 10 * 1024 * 1024;   //10MB
    QString     m_filePath;

};

#endif // LOGGER_H
