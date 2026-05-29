#include "Logger.h"
#include <QDateTime>
#include <QMessageBox>
#include <QTextStream>
#include <cstdlib>
//————单例——————————————————————————————————————————————
Logger& Logger::instance()
{
    static Logger s_instance;           //C++11 保证线程安全的局部静态变量
    return s_instance;
}

Logger::~Logger()
{
    if(m_file.isOpen())
        m_file.close();
}

//————————初始化——————————————————————————————————————————
void Logger::init(const QString& filePath)
{
    m_filePath = filePath;
    m_file.setFileName(filePath);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    //安装全局消息处理器
    qInstallMessageHandler(Logger::messageHandler);
}

void Logger::setMaxFileSize(qint64 bytes)
{
    m_maxFileSize = bytes;
}

// ——消息处理器(static)——————————————————————————————————————————
void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext& ctx,
                            const QString& msg)
{
    //1.格式化时间戳
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    //2.级别标签
    QString level;
    switch (type) {
    case QtDebugMsg:        level = "DEBUG";    break;
    case QtInfoMsg:         level = "INFO";     break;
    case QtWarningMsg:      level = "WARN";     break;
    case QtCriticalMsg:     level = "ERROR";    break;
    case QtFatalMsg:        level = "FATAL";    break;
    }

    //3.组装日志行
    QString line = QString("[%1] [%2] %3").arg(timestamp, level, msg);

    //4.桥接到实例   -   写文件 + 轮转
    Logger& self = instance();
    self.writeLine(line);
    self.rotateIfNeeded();

    //5. ERROR 级别弹窗
    if(type == QtCriticalMsg || type == QtFatalMsg){
        QMessageBox::critical(nullptr, "PulseQt Error", msg);
    }

    //6.FATAL级别终止进程（Qt默认行为）
    if(type == QtFatalMsg)
        std::abort();
}

//——文件写入（非 static）  ————————————————————————

void Logger::writeLine(const QString& line)
{
    if(!m_file.isOpen()) return;
    QTextStream stream(&m_file);
    stream<<line << "\n";
    stream.flush();     //确保立即落盘，崩溃时不丢日志
}

//————文件轮转（非static) ————————————————————————————
void Logger::rotateIfNeeded()
{
    if (m_file.size() <= m_maxFileSize)
        return;

    m_file.close();

    //删除最老的轮转文件，为新文件腾出位置
    QFile::remove(m_filePath + ".5");

    //轮转：app.log.4 -> app.log.5, app.log.3 -> app.log.4, ...
    for(int i = 4; i>= 1; --i){
        QString oldName = m_filePath + "." + QString::number(i);
        QString newName = m_filePath + "." + QString::number(i+1);
        QFile::rename(oldName,newName);
    }

    //当前文件 ->app.log.1
    QFile::rename(m_filePath, m_filePath + ".1");

    //打开新的app.log
    m_file.setFileName(m_filePath);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
