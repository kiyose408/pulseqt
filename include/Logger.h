//==============================================================================
// Logger - 分级日志系统（单例）
//
// 基于 qInstallMessageHandler 拦截全局 Qt 日志（qDebug/qInfo/qWarning/qCritical）。
//
// 默认行为（init 后不做任何配置）：
//   DEBUG → 写文件
//   INFO  → 写文件
//   WARN  → 写文件
//   ERROR → 写文件（可注册 ErrorCallback 追加弹窗/告警）
//   FATAL → 写文件 + std::abort()（可注册 FatalCallback 覆写）
//
// 可配置项：
//   - 最低日志级别（setMinLevel）
//   - 日志行格式（setFormatter）
//   - ERROR 级回调（setErrorCallback）
//   - FATAL 级回调（setFatalCallback，默认 abort）
//   - 文件轮转阈值（setMaxFileSize）
//   - 轮转保留份数（setRotationCount）
//
// 单例模式：Meyer's Singleton
//   static 局部变量在 C++11 起线程安全初始化，无需额外加锁。
//
// 文件轮转：app.log > 10MB → app.log.1（保留份数可配，默认 5）
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
#include <functional>

class Logger
{
public:
    // ── 回调类型 ──
    using ErrorCallback = std::function<void(const QString &msg)>;
    using FatalCallback = std::function<void(const QString &msg)>;
    using LogFormatter  = std::function<QString(QtMsgType type, const QString &msg)>;

    static Logger &instance();                // 单例入口

    // ── 初始化 ──
    void init(const QString &filePath = "app.log");  // 打开日志文件 + 安装消息处理器

    // ── 配置（在 init 之前或之后调用均可） ──
    void setMaxFileSize(qint64 bytes);                // 轮转阈值，默认 10 MB
    void setRotationCount(int count);                 // 保留历史文件数，默认 5
    void setMinLevel(QtMsgType level);                // 屏蔽低于此级别的日志
    void setFormatter(LogFormatter fmt);              // 自定义日志行格式
    void setErrorCallback(ErrorCallback cb);          // ERROR 级额外行为（默认空）
    void setFatalCallback(FatalCallback cb);          // FATAL 级额外行为（默认 abort）

private:
    Logger() = default;                       // 构造私有（单例）
    ~Logger();
    Logger(const Logger &) = delete;          // 禁止拷贝
    Logger &operator=(const Logger &) = delete;

    // static 消息处理器 —— 必须是 static，qInstallMessageHandler 只接受函数指针
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString &msg);

    void writeLine(const QString &line);      // 写入一行日志到文件
    void rotateIfNeeded();                    // 检查是否需要轮转

    QFile          m_file;
    qint64         m_maxFileSize   = 10 * 1024 * 1024;  // 10 MB
    int            m_rotationCount = 5;                 // 保留历史文件数
    QtMsgType      m_minLevel      = QtDebugMsg;        // 最低输出级别
    QString        m_filePath;

    ErrorCallback  m_errorCallback;           // ERROR 级回调（默认空）
    FatalCallback  m_fatalCallback;           // FATAL 级回调（默认 abort）
    LogFormatter   m_formatter;               // 格式器（默认与旧版一致）
};

#endif // LOGGER_H
