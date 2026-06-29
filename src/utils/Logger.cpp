//==============================================================================
// Logger 实现
//==============================================================================

#include "Logger.h"
#include <QDateTime>
#include <QTextStream>

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

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning() << "Logger: cannot open" << filePath;

    qInstallMessageHandler(Logger::messageHandler);
}

void Logger::setMaxFileSize(qint64 bytes)
{
    m_maxFileSize = bytes;
}

void Logger::setRotationCount(int count)
{
    if (count < 1)
        count = 1;
    m_rotationCount = count;
}

void Logger::setMinLevel(QtMsgType level)
{
    m_minLevel = level;
}

void Logger::setFormatter(LogFormatter fmt)
{
    m_formatter = std::move(fmt);
}

void Logger::setErrorCallback(ErrorCallback cb)
{
    m_errorCallback = std::move(cb);
}

void Logger::setFatalCallback(FatalCallback cb)
{
    m_fatalCallback = std::move(cb);
}

//==============================================================================
// static 消息处理器 — 桥接单例
//==============================================================================

void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &ctx,
                            const QString &msg)
{
    Logger &self = instance();

    // 1. 级别过滤
    if (type < self.m_minLevel)
        return;

    // 2. 格式化日志行
    QString line;
    if (self.m_formatter) {
        line = self.m_formatter(type, msg);
    } else {
        // 默认格式：[yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] message
        QString timestamp = QDateTime::currentDateTime()
                                .toString("yyyy-MM-dd hh:mm:ss.zzz");
        QString level;
        switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO";  break;
        case QtWarningMsg:  level = "WARN";  break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
        }
        line = QString("[%1] [%2] %3").arg(timestamp, level, msg);
    }

    // 3. 写文件 + 轮转检查
    self.writeLine(line);
    self.rotateIfNeeded();

    // 4. ERROR / FATAL 额外回调（GUI 弹窗等）
    if ((type == QtCriticalMsg || type == QtFatalMsg) && self.m_errorCallback) {
        self.m_errorCallback(msg);
    }

    // 5. FATAL 终止处理
    if (type == QtFatalMsg) {
        if (self.m_fatalCallback) {
            self.m_fatalCallback(msg);
        } else {
            std::abort();
        }
    }
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

    // 删除最老的备份文件
    QFile::remove(m_filePath + "." + QString::number(m_rotationCount));

    // 轮转：app.log.{N-1} → app.log.{N}, ..., app.log.1 → app.log.2
    for (int i = m_rotationCount - 1; i >= 1; --i) {
        QString oldName = m_filePath + "." + QString::number(i);
        QString newName = m_filePath + "." + QString::number(i + 1);
        QFile::rename(oldName, newName);
    }

    // 当前文件 → app.log.1
    QFile::rename(m_filePath, m_filePath + ".1");

    // 打开新的空 app.log
    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning() << "Logger: cannot reopen after rotate";
}
