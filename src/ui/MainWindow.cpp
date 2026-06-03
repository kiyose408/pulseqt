//==============================================================================
// MainWindow 实现 — QSplitter + 菜单栏 + 工具栏 + 状态栏
//==============================================================================

#include "MainWindow.h"
#include "TcpChannel.h"
#include <QHeaderView>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PulseQt");
    resize(1200, 800);

    setupCentralArea();   // 先创建曲线+表格（setupMenuBar 可能引用）
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
}

void MainWindow::setDataBuffer(DataBuffer *buffer)
{
    m_tableModel->setDataBuffer(buffer);
    m_chart->setDataBuffer(buffer);
}

//==============================================================================
// 菜单栏
//==============================================================================

void MainWindow::setupMenuBar()
{
    // ── 文件 ──
    QMenu *fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("导出 CSV...", this, &MainWindow::onExportCsv);
    fileMenu->addSeparator();
    fileMenu->addAction("退出(&Q)", this, &QWidget::close);

    // ── 视图 ──
    QMenu *viewMenu = menuBar()->addMenu("视图(&V)");
    viewMenu->addAction("显示表格");
    viewMenu->addAction("暗色主题");

    // ── 帮助 ──
    QMenu *helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("关于...", this, &MainWindow::onAbout);
}

//==============================================================================
// 工具栏
//==============================================================================

void MainWindow::setupToolBar()
{
    QToolBar *tb = addToolBar("主工具栏");
    tb->addAction("连接", this, &MainWindow::onConnect);
    tb->addAction("断开", this, &MainWindow::onDisconnect);
    tb->addSeparator();
    tb->addAction("开始", this, &MainWindow::onStart);
    tb->addAction("停止", this, &MainWindow::onStop);
}

//==============================================================================
// 中央区域 — QSplitter 左右分屏
//==============================================================================

void MainWindow::setupCentralArea()
{
    // ── 左侧：自绘曲线 ──
    m_chart = new RealTimeChart(this);

    // ── 右侧：数据表格 ──
    m_tableModel = new DataTableModel(this);
    m_tableView  = new QTableView(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });

    // ── QSplitter 7:3 ──
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_chart);
    splitter->addWidget(m_tableView);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);

    setCentralWidget(splitter);
}

//==============================================================================
// 状态栏
//==============================================================================

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("就绪", this);
    statusBar()->addWidget(m_statusLabel);
}

//==============================================================================
// 槽函数（空壳，后续任务实现）
//==============================================================================

void MainWindow::onExportCsv()
{
    qInfo() << "Export CSV clicked";
}

void MainWindow::onAbout()
{
    QMessageBox::information(this, "关于 PulseQt",
        "PulseQt - 多通道数据采集上位机\n"
        "Qt 6.5+ / C++17 / SQLite\n\n"
        "开发者：Kiyose");
}

void MainWindow::onConnect()
{
    if (m_channelMgr) return;

    if (!m_buffer) {
        m_buffer = new DataBuffer(10000, this);
        setDataBuffer(m_buffer);
    }

    m_decoder = new ProtocolDecoder(this);
    m_channelMgr = new ChannelManager(this);
    m_dbMgr = new DatabaseManager(this);
    m_dbMgr->init("data.db");

    TcpChannel *tcp = new TcpChannel("127.0.0.1", 9999, this);
    m_channelMgr->setChannel(tcp);

    connect(m_channelMgr, &ChannelManager::readyRead, m_decoder, &ProtocolDecoder::feed);
    connect(m_decoder, &ProtocolDecoder::frameDecoded, this, &MainWindow::onFrameDecoded);
    connect(m_channelMgr, &ChannelManager::connected, [this]() { m_statusLabel->setText("已连接"); });
    connect(m_channelMgr, &ChannelManager::disconnected, [this]() {
        if (m_collecting) m_statusLabel->setText("已断开(重连中)");
        else m_statusLabel->setText("已断开");
    });

    m_channelMgr->connectToDevice();
    m_statusLabel->setText("连接中...");
}

void MainWindow::onDisconnect()
{
    m_collecting = false;
    if (!m_channelMgr) return;

    if (IChannel *ch = m_channelMgr->channel())
        ch->close();

    delete m_channelMgr; m_channelMgr = nullptr;
    delete m_decoder;    m_decoder    = nullptr;
    if (m_dbMgr) { m_dbMgr->flush(); delete m_dbMgr; m_dbMgr = nullptr; }
    // m_buffer 保留

    m_statusLabel->setText("已断开");
}

void MainWindow::onStart()
{
    if (!m_channelMgr) onConnect();  // 没连就先连
    m_collecting = true;
    m_statusLabel->setText("采集中...");
}

void MainWindow::onStop()
{
    m_collecting = false;
    m_statusLabel->setText("已暂停");
}

void MainWindow::onFrameDecoded(const Frame &frame)
{
    if (!m_collecting) return;   // 暂停中，忽略数据
    if (frame.type != Frame::TYPE_DATA || frame.payload.size() < 6) return;

    DataPoint dp;
    dp.timestamp = QDateTime::currentMSecsSinceEpoch();
    auto d = reinterpret_cast<const uint8_t*>(frame.payload.constData());
    dp.channels = { double(d[0]|(d[1]<<8)), double(d[2]|(d[3]<<8)), double(d[4]|(d[5]<<8)) };
    m_buffer->push(dp);
    m_dbMgr->insert(dp);
}
