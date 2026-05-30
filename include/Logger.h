//==============================================================================
// Logger - 分级日志系统（单例）
//
// 基于 qInstallMessageHandler 拦截全局 Qt 日志（qDebug/qInfo/qWarning/qCritical）。
//
// 分级策略：
//   DEBUG → 仅写文件
//   INFO  → 仅写文件
//   WARN  → 仅写文件
//   ERROR → 写文件 + QMessageBox 弹窗
//   FATAL → 写文件 + QMessageBox 弹窗 + std::abort()
//
// 单例模式：Meyer's Singleton
//   static 局部变量在 C++11 起线程安全初始化，无需额外加锁。
//
// 文件轮转：app.log > 10MB → app.log.1（最多保留 5 个历史文件）
//
// 桥接模式：qInstallMessageHandler 要求裸函数指针，
//         因此 messageHandler 必须是 static 函数，
//         通过 instance() 拿单例对象访问非 static 成员。
//==============================================================================

#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QString>
#include <QtGlobal>

class Logger
{
public:
    static Logger &instance();                // 单例入口

    void init(const QString &filePath = "app.log");  // 初始化：打开日志文件 + 安装消息处理器
    void setMaxFileSize(qint64 bytes);                // 设置轮转阈值（默认 10MB）

private:
    Logger() = default;                       // 构造私有（单例）
    ~Logger();
    Logger(const Logger &) = delete;          // 禁止拷贝
    Logger &operator=(const Logger &) = delete;

    //==========================================================================
    // static 消息处理器 —— 必须是 static，qInstallMessageHandler 只接受函数指针
    //==========================================================================
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString &msg);

    void writeLine(const QString &line);      // 写入一行日志到文件
    void rotateIfNeeded();                    // 检查是否需要轮转

    QFile   m_file;
    qint64  m_maxFileSize = 10 * 1024 * 1024;  // 10 MB
    QString m_filePath;
};

#endif // LOGGER_H
