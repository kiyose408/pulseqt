//==============================================================================
// MainWindow 实现 — QSplitter + 菜单栏 + 工具栏 + 状态栏
//==============================================================================

#include "MainWindow.h"
#include <QApplication>
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
    viewMenu->addAction("暗色主题", this, &MainWindow::toggleTheme);

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
    // ── 实时曲线 ──────────────────────────────────
    m_chart = new RealTimeChart(this);

    // ── 数据表格 ──────────────────────────────────
    m_tableModel = new DataTableModel(this);
    m_tableView  = new QTableView(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });

    // ── 上半区：实时曲线 + 表格 ─────────────────────
    QSplitter *topSplitter = new QSplitter(Qt::Horizontal, this);
    topSplitter->addWidget(m_chart);
    topSplitter->addWidget(m_tableView);
    topSplitter->setStretchFactor(0, 7);
    topSplitter->setStretchFactor(1, 3);

    // ── 回放曲线（独立 RealTimeChart，绑定独立 DataBuffer） ──
    m_playbackChart = new RealTimeChart(this);
    m_playbackChart->setMinimumHeight(150);

    // ── 历史回放滑块 ──────────────────────────────
    m_historyPlayer = new HistoryPlayer(this);

    // ── 整体垂直布局 ──────────────────────────────
    QWidget *central = new QWidget(this);
    QVBoxLayout *vLayout = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(topSplitter, 3);          // 上半区占 3 份
    vLayout->addWidget(m_playbackChart, 1);       // 回放曲线占 1 份
    vLayout->addWidget(m_historyPlayer);          // 底部滑块

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
    // 已连接 → 先断（用户可能想切通道/改配置）
    if (m_connected)
        onDisconnect();

    // ── 弹对话框（每次连接都弹，可换通道） ────────────
    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QString    channelId = dlg.selectedChannelId();
    QVariantMap cfg      = dlg.config();

    // ── 首次连接：创建线程 + 管理器 + 解析器 ──────────
    if (!m_parseWorker) {
        m_commThread     = new QThread(this);
        m_parseThread    = new QThread(this);
        m_channelManager = new ChannelManager();
        m_parseWorker    = new ParseWorker();

        m_channelManager->moveToThread(m_commThread);
        m_parseWorker->moveToThread(m_parseThread);

        connect(m_channelManager, &ChannelManager::readyRead,
                m_parseWorker, &ParseWorker::onRawDataReceived,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::writeData,
                m_channelManager, &ChannelManager::writeData,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::dataPointReady,
                this, [this]() {}, Qt::QueuedConnection);
        connect(m_channelManager, &ChannelManager::connected,
                this, [this]() { m_statusLabel->setText("已连接"); },
                Qt::QueuedConnection);
        connect(m_channelManager, &ChannelManager::disconnected,
                this, [this]() {
                    if (m_collecting) m_statusLabel->setText("已断开(重连中)");
                    else m_statusLabel->setText("已断开");
                }, Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::handshakeCompleted,
                this, [this](int count, const QVector<int> &types) {
            m_tableModel->setChannelCount(count);
            m_statusLabel->setText(QString("已连接 · %1 通道").arg(count));
            Q_UNUSED(types);
        }, Qt::QueuedConnection);

        setDataBuffer(m_parseWorker->buffer());
        m_historyPlayer->setDbPath("data.db");
        m_historyPlayer->setDataBuffer(m_parseWorker->buffer());
        // 回放图表永久绑定独立 buffer（不切换，不和实时抢）
        m_playbackChart->setDataBuffer(m_historyPlayer->playbackBuffer());
        m_historyPlayer->setChart(m_playbackChart);
        m_historyPlayer->setTimeWindow(m_chart->timeWindow());
        m_historyPlayer->loadTimeRange();

        m_commThread->start();
        m_parseThread->start();
    }

    // ── 在通信线程内创建通道 → 设置 → 连接 ────────────
    //    通道在通信线程创建，避免 setParent 跨线程警告
    QMetaObject::invokeMethod(m_channelManager, [this, channelId, cfg]() {
        auto *ch = ChannelRegistry::create(channelId, cfg);
        if (!ch) return;
        m_channelManager->setChannel(ch);
        m_channelManager->connectToDevice();
    }, Qt::QueuedConnection);

    m_connected = true;
    m_statusLabel->setText("连接中...");
}

void MainWindow::onDisconnect()
{
    if (!m_connected) return;

    m_collecting = false;
    m_connected  = false;

    // 通知 ParseWorker 停止采集 + 重置通道配置
    if (m_parseWorker) {
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
        QMetaObject::invokeMethod(m_parseWorker, "resetChannelConfig",
                                  Qt::QueuedConnection);
    }

    // 关闭通道（ParseWorker + DataBuffer + 线程存活，历史回放可用）
    QMetaObject::invokeMethod(m_channelManager, "disconnectDevice",
                              Qt::QueuedConnection);

    m_statusLabel->setText("已断开");
}

// ── 暗色主题切换 ──────────────────────────────────
void MainWindow::toggleTheme()
{
    m_darkTheme = !m_darkTheme;

    if (m_darkTheme) {
        // Fusion + 暗色调色板
        QApplication::setStyle("Fusion");
        QPalette p;
        p.setColor(QPalette::Window,          QColor(0x2D,0x2D,0x30));
        p.setColor(QPalette::WindowText,      QColor(0xDC,0xDC,0xDC));
        p.setColor(QPalette::Base,            QColor(0x1E,0x1E,0x1E));
        p.setColor(QPalette::AlternateBase,   QColor(0x2A,0x2A,0x2E));
        p.setColor(QPalette::Button,          QColor(0x3E,0x3E,0x42));
        p.setColor(QPalette::ButtonText,      QColor(0xDC,0xDC,0xDC));
        p.setColor(QPalette::Text,            QColor(0xDC,0xDC,0xDC));
        p.setColor(QPalette::Highlight,       QColor(0x00,0x7A,0xCC));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::ToolTipBase,     QColor(0x2D,0x2D,0x30));
        p.setColor(QPalette::ToolTipText,     QColor(0xDC,0xDC,0xDC));
        QApplication::setPalette(p);
    } else {
        QApplication::setStyle("");
        QApplication::setPalette(QApplication::style()->standardPalette());
    }

    if (m_chart)          m_chart->setDarkMode(m_darkTheme);
    if (m_playbackChart)  m_playbackChart->setDarkMode(m_darkTheme);
}

// ── 全部拆光：仅窗口关闭时调用 ──────────────────────
void MainWindow::teardown()
{
    m_collecting = false;
    m_connected  = false;

    if (m_channelManager) {
        QMetaObject::invokeMethod(m_channelManager, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
    }

    if (m_parseWorker) {
        QMetaObject::invokeMethod(m_parseWorker, "resetChannelConfig",
                                  Qt::BlockingQueuedConnection);
    }

    if (m_commThread) {
        m_commThread->quit();
        m_commThread->wait();
    }
    if (m_parseThread) {
        m_parseThread->quit();
        m_parseThread->wait();
    }

    // 线程已停，安全直接删除（通道已关闭）
    delete m_channelManager; m_channelManager = nullptr;
    delete m_parseWorker;    m_parseWorker    = nullptr;
    delete m_commThread;     m_commThread     = nullptr;
    delete m_parseThread;    m_parseThread    = nullptr;
}

void MainWindow::onStart()
{
    if (!m_parseWorker || !m_connected) return;
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


