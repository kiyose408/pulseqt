//==============================================================================
// SerialChannel 实现
//
// 关键设计：
// 1. 构造函数只存参数，open() 才 new QSerialPort —— 支持 close 后换端口再 open
// 2. 信号级联：QSerialPort 的内部事件 → lambda 读取/转换 → emit IChannel 信号
// 3. 析构时自动 close()，防止串口泄漏
// 4. open() 是同步的：返回 true = 已连上，返回 false = 失败
//==============================================================================

#include "SerialChannel.h"
#include <QDebug>
#include <QTimer>

//==============================================================================
// 构造 / 析构
//==============================================================================

SerialChannel::SerialChannel(const QString &portName, qint32 baudRate,
                             QSerialPort::DataBits dataBits,
                             QSerialPort::StopBits stopBits,
                             QSerialPort::Parity parity,
                             QObject *parent)
    : IChannel(parent)            // 先初始化基类 QObject
    , m_portName(portName)        // 存储配置参数（不立即打开）
    , m_baudRate(baudRate)
    , m_dataBits(dataBits)
    , m_stopBits(stopBits)
    , m_parity(parity)
{}

SerialChannel::~SerialChannel()
{
    close();   // 确保析构时释放串口资源
}

//==============================================================================
// IChannel 接口实现
//==============================================================================

bool SerialChannel::open()
{
    // 已打开则直接返回
    if (m_serialPort)
        return m_serialPort->isOpen();

    // 创建 QSerialPort 对象（parent = this，随 SerialChannel 生命周期管理）
    m_serialPort = new QSerialPort(m_portName, this);

    // 应用构造时存储的串口参数
    m_serialPort->setBaudRate(m_baudRate);
    m_serialPort->setDataBits(m_dataBits);
    m_serialPort->setStopBits(m_stopBits);
    m_serialPort->setParity(m_parity);
    m_serialPort->setReadBufferSize(0);  // 无限缓冲，防 ring buffer 溢出

    // 同步打开物理串口（阻塞，立即返回结果）
    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_serialPort->errorString());
        return false;
    }

    //==========================================================================
    // 信号级联：QSerialPort 事件 → IChannel 信号
    // 为什么要转发？上层（ChannelManager、数据层）只依赖 IChannel 接口，
    // 不直接引用 QSerialPort，换通道类型时无需改上层代码
    //==========================================================================

    // 收到数据 → 读取全部 → 转发给上层
    connect(m_serialPort, &QSerialPort::readyRead, this, [this]() {
        qint64 avail = m_serialPort->bytesAvailable();
        if (avail <= 0) return;                  // 防止 ring buffer 状态异常
        QByteArray data = m_serialPort->read(avail);
        if (!data.isEmpty()) emit readyRead(data);
    });

    // 串口错误 → 转发错误信息
    connect(m_serialPort, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError)
            emit errorOccurred(m_serialPort->errorString());
    });

    // ── 缓冲区状态监控（压测用，10 秒一次）─────────
    auto *monitor = new QTimer(this);
    monitor->setInterval(10000);
    connect(monitor, &QTimer::timeout, this, [this]() {
        qint64 avail = m_serialPort ? m_serialPort->bytesAvailable() : -1;
        if (avail > 100)
            qWarning() << "[串口缓冲] 积压" << avail << "字节 — 可能溢出!";
        else if (avail >= 0)
            qDebug() << "[串口缓冲] normal:" << avail << "字节";
    });
    monitor->start();

    emit connected();
    qInfo() << "SerialChannel opened:" << m_portName << "@" << m_baudRate;
    return true;
}

void SerialChannel::close()
{
    if (m_serialPort) {
        m_serialPort->disconnect();
        m_serialPort->readAll();      // 清空内部 ring buffer，防止 close 时 ASSERT
        m_serialPort->close();
        delete m_serialPort;
        m_serialPort = nullptr;
        emit disconnected();
    }
}

bool SerialChannel::isOpen() const
{
    // 指针非空 且 底层串口处于打开状态
    return m_serialPort && m_serialPort->isOpen();
}

qint64 SerialChannel::write(const QByteArray &data)
{
    if (!m_serialPort || !m_serialPort->isOpen()) {
        emit errorOccurred("Serial port not open");
        return -1;
    }
    qint64 n = m_serialPort->write(data);
    if (n < 0) emit errorOccurred(m_serialPort->errorString());
    return n;
}

// ── 自注册到全局通道注册表 ──────────────────────────
#include "ChannelRegistry.h"
#include <QSerialPort>
static const bool serialRegistered = []() {
    ChannelDescriptor desc;
    desc.id   = "serial";
    desc.name = "串口";
    desc.configFields = {
#ifdef Q_OS_WIN
        ConfigField("portName", "串口号", "string", "COM3"),
#else
        ConfigField("portName", "串口号", "string", "/dev/ttyUSB0"),
#endif
        ConfigField("baudRate", "波特率", "combo", 115200,
            {"9600","19200","38400","57600","115200","230400","460800","921600"}),
        ConfigField("dataBits", "数据位", "combo", 8,
            {"5","6","7","8"}),
        ConfigField("stopBits", "停止位", "combo", 1,
            {"1","1.5","2"}),
        ConfigField("parity",   "校验位", "combo", "None",
            {"None","Even","Odd"}),
    };
    desc.factory = +[](const QVariantMap &cfg, QObject *parent) -> IChannel* {
        auto parity = QSerialPort::NoParity;
        QString p = cfg.value("parity", "None").toString();
        if (p == "Even") parity = QSerialPort::EvenParity;
        else if (p == "Odd") parity = QSerialPort::OddParity;

        auto stop = QSerialPort::OneStop;
        if (cfg.value("stopBits").toDouble() >= 2.0) stop = QSerialPort::TwoStop;
        else if (cfg.value("stopBits").toDouble() >= 1.5) stop = QSerialPort::OneAndHalfStop;

        return new SerialChannel(
            cfg["portName"].toString(),
            cfg["baudRate"].toInt(),
            static_cast<QSerialPort::DataBits>(cfg["dataBits"].toInt()),
            stop, parity, parent);
    };
    ChannelRegistry::registerChannel(desc);
    return true;
}();
