//==============================================================================
// Logger 实现
//==============================================================================

#include "Logger.h"
#include <QDateTime>
#include <QTextStream>
#include <QMessageBox>
#include <iostream>

//==============================================================================
// 单例
//==============================================================================

Logger &Logger::instance()
{
    // C++11 起局部 static 变量初始化是线程安全的，无需加锁
    static Logger s_instance;
    return s_instance;
}

Logger::~Logger()
{
    if (m_file.isOpen())
        m_file.close();
}

//==============================================================================
// 初始化
//==============================================================================

void Logger::init(const QString &filePath)
{
    m_filePath = filePath;
    m_file.setFileName(filePath);

    // 以追加 + 文本模式打开
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    // 安装全局消息处理器（替换 Qt 默认的控制台输出）
    qInstallMessageHandler(Logger::messageHandler);
}

void Logger::setMaxFileSize(qint64 bytes)
{
    m_maxFileSize = bytes;
}

//==============================================================================
// static 消息处理器 — 桥接单例
//==============================================================================

void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &ctx,
                            const QString &msg)
{
    // 1. 时间戳（精确到毫秒）
    QString timestamp = QDateTime::currentDateTime()
                            .toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 2. 日志级别标签
    QString level;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO";  break;
    case QtWarningMsg:  level = "WARN";  break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    // 3. 组装日志行：[时间戳] [级别] 消息内容
    QString line = QString("[%1] [%2] %3")
                       .arg(timestamp, level, msg);

    // 4. 桥接：通过单例获取实例，调用非 static 方法
    Logger &self = instance();
    self.writeLine(line);
    self.rotateIfNeeded();

    // 5. ERROR/FATAL 级别额外弹窗提醒用户
    if (type == QtCriticalMsg || type == QtFatalMsg) {
        QMessageBox::critical(nullptr, "PulseQt Error", msg);
    }

    // 6. FATAL 终止进程（Qt 默认行为）
    if (type == QtFatalMsg)
        std::abort();
}

//==============================================================================
// 文件写入
//==============================================================================

void Logger::writeLine(const QString &line)
{
    if (!m_file.isOpen()) return;

    QTextStream stream(&m_file);
    stream << line << "\n";
    stream.flush();   // 立即刷盘，崩溃时也不丢最后的日志
}

//==============================================================================
// 文件轮转
//==============================================================================

void Logger::rotateIfNeeded()
{
    if (m_file.size() <= m_maxFileSize)
        return;   // 未超阈值，不轮转

    m_file.close();

    // 删除最老的备份文件（app.log.5）
    QFile::remove(m_filePath + ".5");

    // 轮转：app.log.4 → app.log.5, app.log.3 → app.log.4, ...
    for (int i = 4; i >= 1; --i) {
        QString oldName = m_filePath + "." + QString::number(i);
        QString newName = m_filePath + "." + QString::number(i + 1);
        QFile::rename(oldName, newName);
    }

    // 当前文件 → app.log.1
    QFile::rename(m_filePath, m_filePath + ".1");

    // 打开新的空 app.log
    m_file.setFileName(m_filePath);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
