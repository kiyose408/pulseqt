//==============================================================================
// MainWindow 实现 — QSplitter + 菜单栏 + 工具栏 + 状态栏
//==============================================================================

#include "MainWindow.h"
#include "TcpChannel.h"
#include <QCloseEvent>
#include <QHeaderView>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include "ExportDialog.h"
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    teardown();
    event->accept();
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
    m_chart = new RealTimeChart(this);
    m_tableModel = new DataTableModel(this);
    m_tableView  = new QTableView(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });

    // 上：曲线 + 表格
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_chart);
    splitter->addWidget(m_tableView);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);

    // 下：历史回放
    m_historyPlayer = new HistoryPlayer(this);

    // 整体垂直布局
    QWidget *central = new QWidget(this);
    QVBoxLayout *vLayout = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(splitter, 1);         // 占满剩余空间
    vLayout->addWidget(m_historyPlayer);     // 底部固定高度

    setCentralWidget(central);
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
    DatabaseManager db;                    // 临时独立连接
    if (!db.init("data.db")) {
        QMessageBox::warning(this, "错误", "无法打开数据库");
        return;
    }

    ExportDialog dialog(&db, this);       // 传独立连接
    dialog.exec();                         // 模态弹窗
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
    if (m_connected) return;   // 已连接

    // ── 首次连接：创建线程 + 通道 + 解析器 ──────────
    if (!m_parseWorker) {
        m_commThread  = new QThread(this);
        m_parseThread = new QThread(this);

        auto *tcpChannel = new TcpChannel("127.0.0.1", 9999);
        m_channelManager = new ChannelManager();
        m_channelManager->setChannel(tcpChannel);
        m_parseWorker    = new ParseWorker();

        m_channelManager->moveToThread(m_commThread);
        m_parseWorker->moveToThread(m_parseThread);

        // 信号接线：ChannelManager ↔ ParseWorker
        connect(m_channelManager, &ChannelManager::readyRead,
                m_parseWorker, &ParseWorker::onRawDataReceived,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::writeData,
                m_channelManager, &ChannelManager::writeData,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::dataPointReady,
                this, [this]() {}, Qt::QueuedConnection);

        // 状态更新
        connect(m_channelManager, &ChannelManager::connected,
                this, [this]() { m_statusLabel->setText("已连接"); },
                Qt::QueuedConnection);
        connect(m_channelManager, &ChannelManager::disconnected,
                this, [this]() {
                    if (m_collecting) m_statusLabel->setText("已断开(重连中)");
                    else m_statusLabel->setText("已断开");
                }, Qt::QueuedConnection);

        // 注入数据源
        setDataBuffer(m_parseWorker->buffer());
        m_historyPlayer->setDbPath("data.db");
        m_historyPlayer->setDataBuffer(m_parseWorker->buffer());
        m_historyPlayer->loadTimeRange();

        m_commThread->start();
        m_parseThread->start();
    }

    // ── 发起连接（首次 / 重连） ──────────────────────
    QMetaObject::invokeMethod(m_channelManager, "connectToDevice",
                              Qt::QueuedConnection);
    m_connected = true;
    m_statusLabel->setText("连接中...");
}

void MainWindow::onDisconnect()
{
    if (!m_connected) return;

    m_collecting = false;
    m_connected  = false;

    // 通知 ParseWorker 停止采集
    if (m_parseWorker)
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting",
                                  Qt::QueuedConnection, Q_ARG(bool, false));

    // 关闭通道（ParseWorker + DataBuffer + 线程存活，历史回放可用）
    QMetaObject::invokeMethod(m_channelManager, "disconnectDevice",
                              Qt::QueuedConnection);

    m_statusLabel->setText("已断开");
}

// ── 全部拆光：仅窗口关闭时调用 ──────────────────────
void MainWindow::teardown()
{
    m_collecting = false;
    m_connected  = false;

    if (m_channelManager) {
        QMetaObject::invokeMethod(m_channelManager, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
        m_channelManager->deleteLater();
        m_channelManager = nullptr;
    }
    if (m_parseWorker) {
        m_parseWorker->deleteLater();
        m_parseWorker = nullptr;
    }

    if (m_commThread) {
        m_commThread->quit();
        m_commThread->wait();
        delete m_commThread;
        m_commThread = nullptr;
    }
    if (m_parseThread) {
        m_parseThread->quit();
        m_parseThread->wait();
        delete m_parseThread;
        m_parseThread = nullptr;
    }
}

void MainWindow::onStart()
{
    if (!m_parseWorker) return;
    QMetaObject::invokeMethod(m_parseWorker, "setCollecting",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    m_collecting = true;
    m_statusLabel->setText("采集中...");
}

void MainWindow::onStop()
{
    m_collecting = false;
    if (m_parseWorker)
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
    m_statusLabel->setText("已暂停");
}


