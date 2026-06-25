//==============================================================================
// MagMainWindow 实现 — 双通道管道 + FFT + 相位波形展示
//==============================================================================

#include "MagMainWindow.h"
#include "Logger.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QtMath>

MagMainWindow::MagMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PulseQt v2.0 — 磁场相位分析");
    resize(1200, 800);

    setupCentralArea();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
}

MagMainWindow::~MagMainWindow()
{
    teardownAll();
}

void MagMainWindow::closeEvent(QCloseEvent *event)
{
    teardownAll();
    event->accept();
}

//==============================================================================
// 中央区域
//==============================================================================

void MagMainWindow::setupCentralArea()
{
    // ── 相位波形图 ────────────────────────────────────
    m_phaseChart = new RealTimeChart(this);
    m_diffChart  = new RealTimeChart(this);
    m_diffChart->setMinimumHeight(150);

    // 相位缓冲
    m_phaseBuf = new DataBuffer(5000, this);   // 50Hz × 100s
    m_diffBuf  = new DataBuffer(5000, this);

    m_phaseChart->setDataBuffer(m_phaseBuf);
    m_diffChart->setDataBuffer(m_diffBuf);

    QWidget *central = new QWidget(this);
    QVBoxLayout *vLayout = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(m_phaseChart, 3);
    vLayout->addWidget(m_diffChart, 1);
    setCentralWidget(central);
}

//==============================================================================
// 菜单栏
//==============================================================================

void MagMainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("退出(&Q)", this, &QWidget::close);
    QMenu *helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("关于...", this, [this]() {
        QMessageBox::information(this, "关于 PulseQt v2.0",
            "PulseQt v2.0 — 磁场相位分析\n"
            "双通道 (TCP + 串口) 磁场数据采集 + FFT 50Hz 相位提取");
    });
}

//==============================================================================
// 工具栏
//==============================================================================

void MagMainWindow::setupToolBar()
{
    QToolBar *tb = addToolBar("主工具栏");
    tb->addAction("连接TCP",  this, &MagMainWindow::onConnectTcp);
    tb->addAction("断开TCP",  this, &MagMainWindow::onDisconnectTcp);
    tb->addSeparator();
    tb->addAction("连接串口", this, &MagMainWindow::onConnectSerial);
    tb->addAction("断开串口", this, &MagMainWindow::onDisconnectSerial);
}

//==============================================================================
// 状态栏
//==============================================================================

void MagMainWindow::setupStatusBar()
{
    m_statusTcp    = new QLabel("TCP: 未连接", this);
    m_statusSerial = new QLabel("串口: 未连接", this);
    m_statusFft    = new QLabel("FFT: 待启动", this);
    statusBar()->addWidget(m_statusTcp);
    statusBar()->addWidget(m_statusSerial);
    statusBar()->addWidget(m_statusFft);
}

//════════════════════════════════════════════════════════════════════════════
// TCP 管道
//════════════════════════════════════════════════════════════════════════════

void MagMainWindow::createTcpPipeline()
{
    m_tcpCommThread  = new QThread(this);
    m_tcpParseThread = new QThread(this);

    m_tcpChannelManager = new ChannelManager();
    m_tcpChannelManager->moveToThread(m_tcpCommThread);

    m_tcpDecoder = new MagCollectorDecoder();
    m_tcpDecoder->moveToThread(m_tcpParseThread);

    m_tcpMagBuffer = new MagDataBuffer(2000, this);  // 1000Hz × 2s

    connect(m_tcpChannelManager, &ChannelManager::readyRead,
            m_tcpDecoder, &MagCollectorDecoder::feed, Qt::QueuedConnection);

    connect(m_tcpDecoder, &MagCollectorDecoder::frameDecoded,
            this, [this](const MagCollectorFrame &frame) {
        double magX1 = MagFormula::fromAdc(frame.data[0]);
        double magY1 = MagFormula::fromAdc(frame.data[1]);
        qint64 ts = QDateTime::currentMSecsSinceEpoch();
        m_tcpMagBuffer->push(MagDataPoint::dual(magX1, magY1, ts));
    }, Qt::QueuedConnection);

    connect(m_tcpChannelManager, &ChannelManager::connected,
            this, [this]() { m_statusTcp->setText("TCP: 已连接"); },
            Qt::QueuedConnection);
    connect(m_tcpChannelManager, &ChannelManager::disconnected,
            this, [this]() { m_statusTcp->setText("TCP: 已断开"); },
            Qt::QueuedConnection);

    m_tcpCommThread->start();
    m_tcpParseThread->start();
}

void MagMainWindow::onConnectTcp()
{
    if (m_tcpConnected) onDisconnectTcp();
    if (!m_tcpChannelManager) createTcpPipeline();

    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString id = dlg.selectedChannelId();
    QVariantMap cfg = dlg.config();

    QMetaObject::invokeMethod(m_tcpChannelManager, [this, id, cfg]() {
        auto *ch = ChannelRegistry::create(id, cfg);
        if (ch) { m_tcpChannelManager->setChannel(ch); m_tcpChannelManager->connectToDevice(); }
    }, Qt::QueuedConnection);

    m_tcpConnected = true;
    m_statusTcp->setText("TCP: 连接中...");

    // 尝试启动 FFT
    createFftProcessors();
}

void MagMainWindow::onDisconnectTcp()
{
    if (!m_tcpConnected) return;
    m_tcpConnected = false;
    QMetaObject::invokeMethod(m_tcpChannelManager, "disconnectDevice", Qt::QueuedConnection);
    m_statusTcp->setText("TCP: 已断开");
}

void MagMainWindow::teardownTcpPipeline()
{
    m_tcpConnected = false;
    if (m_tcpChannelManager) {
        // 1. 在通信线程内断开连接（也会停重连定时器）
        QMetaObject::invokeMethod(m_tcpChannelManager, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
    }
    // 2. 停线程 → 此时通信线程已无活动 timer
    if (m_tcpCommThread)  { m_tcpCommThread->quit();  m_tcpCommThread->wait(); }
    if (m_tcpParseThread) { m_tcpParseThread->quit(); m_tcpParseThread->wait(); }
    // 3. 安全删除（线程已死，timer已停，不会触发跨线程stop）
    delete m_tcpChannelManager; m_tcpChannelManager = nullptr;
    delete m_tcpDecoder;        m_tcpDecoder        = nullptr;
    delete m_tcpCommThread;     m_tcpCommThread     = nullptr;
    delete m_tcpParseThread;    m_tcpParseThread    = nullptr;
}

//════════════════════════════════════════════════════════════════════════════
// 串口管道
//════════════════════════════════════════════════════════════════════════════

void MagMainWindow::createSerialPipeline()
{
    m_serCommThread  = new QThread(this);
    m_serParseThread = new QThread(this);

    m_serChannelManager = new ChannelManager();
    m_serChannelManager->moveToThread(m_serCommThread);

    m_serParser = new DollarParser();
    m_serParser->moveToThread(m_serParseThread);

    m_serMagBuffer = new MagDataBuffer(500, this);  // 250Hz × 2s

    connect(m_serChannelManager, &ChannelManager::readyRead,
            m_serParser, &DollarParser::feed, Qt::QueuedConnection);

    connect(m_serParser, &DollarParser::valueReady,
            this, [this](double value, qint64 /*ts*/) {
        double mag = MagFormula::fromSerial(value);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_serMagBuffer->push(MagDataPoint::single(mag, now));
    }, Qt::QueuedConnection);

    connect(m_serChannelManager, &ChannelManager::connected,
            this, [this]() { m_statusSerial->setText("串口: 已连接"); },
            Qt::QueuedConnection);
    connect(m_serChannelManager, &ChannelManager::disconnected,
            this, [this]() { m_statusSerial->setText("串口: 已断开"); },
            Qt::QueuedConnection);

    m_serCommThread->start();
    m_serParseThread->start();
}

void MagMainWindow::onConnectSerial()
{
    if (m_serConnected) onDisconnectSerial();
    if (!m_serChannelManager) createSerialPipeline();

    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString id = dlg.selectedChannelId();
    QVariantMap cfg = dlg.config();

    QMetaObject::invokeMethod(m_serChannelManager, [this, id, cfg]() {
        auto *ch = ChannelRegistry::create(id, cfg);
        if (ch) { m_serChannelManager->setChannel(ch); m_serChannelManager->connectToDevice(); }
    }, Qt::QueuedConnection);

    m_serConnected = true;
    m_statusSerial->setText("串口: 连接中...");

    createFftProcessors();
}

void MagMainWindow::onDisconnectSerial()
{
    if (!m_serConnected) return;
    m_serConnected = false;
    QMetaObject::invokeMethod(m_serChannelManager, "disconnectDevice", Qt::QueuedConnection);
    m_statusSerial->setText("串口: 已断开");
}

void MagMainWindow::teardownSerialPipeline()
{
    m_serConnected = false;
    if (m_serChannelManager) {
        QMetaObject::invokeMethod(m_serChannelManager, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
    }
    if (m_serCommThread)  { m_serCommThread->quit();  m_serCommThread->wait(); }
    if (m_serParseThread) { m_serParseThread->quit(); m_serParseThread->wait(); }
    delete m_serChannelManager; m_serChannelManager = nullptr;
    delete m_serParser;         m_serParser         = nullptr;
    delete m_serCommThread;     m_serCommThread     = nullptr;
    delete m_serParseThread;    m_serParseThread    = nullptr;
}

//════════════════════════════════════════════════════════════════════════════
// FFT 处理器
//════════════════════════════════════════════════════════════════════════════

void MagMainWindow::createFftProcessors()
{
    // 确保管道已就绪
    if (m_fftRunning) return;
    if (!m_tcpChannelManager || !m_serChannelManager) return;

    m_fftThread = new QThread(this);

    // TCP X1: N=1000, fs=1000
    FftProcessor::Config cfgTcp;
    cfgTcp.sampleRate = 1000; cfgTcp.fftSize = 1000; cfgTcp.targetFreq = 50.0;
    cfgTcp.updateIntervalMs = 20;

    m_fftX1 = new FftProcessor(cfgTcp, m_tcpMagBuffer,
        [](const MagDataPoint &p) { return p.data[0]; });   // X1

    // TCP Y1: 共用配置
    m_fftY1 = new FftProcessor(cfgTcp, m_tcpMagBuffer,
        [](const MagDataPoint &p) { return p.data[1]; });   // Y1

    // Serial: N=250, fs=250
    FftProcessor::Config cfgSer;
    cfgSer.sampleRate = 250; cfgSer.fftSize = 250; cfgSer.targetFreq = 50.0;
    cfgSer.updateIntervalMs = 20;

    m_fftSerial = new FftProcessor(cfgSer, m_serMagBuffer,
        [](const MagDataPoint &p) { return p.data[0]; });

    // 移动到 FFT 线程
    m_fftX1->moveToThread(m_fftThread);
    m_fftY1->moveToThread(m_fftThread);
    m_fftSerial->moveToThread(m_fftThread);

    // 相位信号 → 主线程更新
    connect(m_fftX1, &FftProcessor::phaseReady,
            this, &MagMainWindow::onPhaseTcpX1, Qt::QueuedConnection);
    connect(m_fftY1, &FftProcessor::phaseReady,
            this, &MagMainWindow::onPhaseTcpY1, Qt::QueuedConnection);
    connect(m_fftSerial, &FftProcessor::phaseReady,
            this, &MagMainWindow::onPhaseSerial, Qt::QueuedConnection);

    // 启动 FFT（在 FFT 线程中启动定时器）
    QMetaObject::invokeMethod(m_fftX1, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_fftY1, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_fftSerial, "start", Qt::QueuedConnection);

    m_fftThread->start();
    m_fftRunning = true;
    m_statusFft->setText("FFT: 运行中");
}

static double normAngle(double deg)
{
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
}

static double unwrapPhase(double raw, double &prev, double &accum, bool &init)
{
    if (!init) {
        accum = raw;
        prev  = raw;
        init  = true;
        return raw;
    }
    double delta = raw - prev;
    delta = normAngle(delta);
    accum += delta;
    prev   = raw;
    return accum;
}

void MagMainWindow::onPhaseTcpX1(double deg, qint64 /*ts*/)
{
    m_phiX1 = deg;
    m_hasX1 = true;
    m_unwrapX1 = unwrapPhase(deg, m_unwrapPrevX1, m_unwrapX1, m_unwrapInitX1);
    pushPhaseDisplay();
}

void MagMainWindow::onPhaseTcpY1(double deg, qint64 /*ts*/)
{
    m_phiY1 = deg;
    m_hasY1 = true;
    m_unwrapY1 = unwrapPhase(deg, m_unwrapPrevY1, m_unwrapY1, m_unwrapInitY1);
    pushPhaseDisplay();
}

void MagMainWindow::onPhaseSerial(double deg, qint64 /*ts*/)
{
    m_phiSerial = deg;
    m_hasSerial = true;
    m_unwrapSerial = unwrapPhase(deg, m_unwrapPrevSerial, m_unwrapSerial, m_unwrapInitSerial);
    pushPhaseDisplay();
}

void MagMainWindow::pushPhaseDisplay()
{
    qint64 ts = QDateTime::currentMSecsSinceEpoch();

    // ── 相位波形：unwrapped ──────────────────────────
    {
        DataPoint dp;
        dp.timestamp = static_cast<uint64_t>(ts);
        dp.channels.resize(3);
        dp.channels[0] = m_hasX1 ? m_phiX1 : 0.0;
        dp.channels[1] = m_hasY1 ? m_phiY1 : 0.0;
        dp.channels[2] = m_hasSerial ? m_phiSerial : 0.0;
        m_phaseBuf->push(dp);
    }

    // ── 相位差波形：保持归一化 [-180, +180] ───────────
    if (m_hasX1 && m_hasSerial) {
        DataPoint dp;
        dp.timestamp = static_cast<uint64_t>(ts);
        dp.channels.resize(2);
        dp.channels[0] = normAngle(m_phiX1 - m_phiSerial);
        dp.channels[1] = m_hasY1 ? normAngle(m_phiY1 - m_phiSerial) : 0.0;
        m_diffBuf->push(dp);
    }
}

void MagMainWindow::teardownFft()
{
    m_fftRunning = false;

    // 跨线程停止 FFT 定时器（必须用 invokeMethod）
    if (m_fftX1)     QMetaObject::invokeMethod(m_fftX1, "stop", Qt::BlockingQueuedConnection);
    if (m_fftY1)     QMetaObject::invokeMethod(m_fftY1, "stop", Qt::BlockingQueuedConnection);
    if (m_fftSerial) QMetaObject::invokeMethod(m_fftSerial, "stop", Qt::BlockingQueuedConnection);

    if (m_fftThread) { m_fftThread->quit(); m_fftThread->wait(); }

    delete m_fftX1;     m_fftX1     = nullptr;
    delete m_fftY1;     m_fftY1     = nullptr;
    delete m_fftSerial; m_fftSerial = nullptr;
    delete m_fftThread; m_fftThread = nullptr;
}

//════════════════════════════════════════════════════════════════════════════
// 全局清理
//════════════════════════════════════════════════════════════════════════════

void MagMainWindow::teardownAll()
{
    if (m_tearingDown) return;
    m_tearingDown = true;

    teardownFft();
    teardownTcpPipeline();
    teardownSerialPipeline();
}
