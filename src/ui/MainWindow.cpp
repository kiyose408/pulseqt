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
    onDisconnect();    // 关闭前清理线程
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
    if (m_commThread) return;   // 已连接

    // ① 创建线程
    m_commThread  = new QThread(this);
    m_parseThread = new QThread(this);

    // ② 创建 Worker（无 parent，之后 moveToThread 再设归属）
    m_tcpWorker   = new TcpWorker("127.0.0.1", 9999);
    m_parseWorker = new ParseWorker();

    // ③ 移入对应线程
    m_tcpWorker->moveToThread(m_commThread);
    m_parseWorker->moveToThread(m_parseThread);

    // ④ 信号串联（全部 QueuedConnection）
    connect(m_tcpWorker, &TcpWorker::rawDataReceived,
            m_parseWorker, &ParseWorker::onRawDataReceived,
            Qt::QueuedConnection);

    // 反向通道：ParseWorker 发心跳帧 → TcpWorker 发送
    connect(m_parseWorker, &ParseWorker::writeData,
            m_tcpWorker, &TcpWorker::write,
            Qt::QueuedConnection);

    connect(m_parseWorker, &ParseWorker::dataPointReady,
            this, [this]() { /* UI 稍后刷新 */ },
            Qt::QueuedConnection);

    connect(m_tcpWorker, &TcpWorker::connected,
            this, [this]() { m_statusLabel->setText("已连接"); },
            Qt::QueuedConnection);

    connect(m_tcpWorker, &TcpWorker::disconnected,
            this, [this]() {
                if (m_collecting) m_statusLabel->setText("已断开(重连中)");
                else m_statusLabel->setText("已断开");
            }, Qt::QueuedConnection);

    // ⑤ 注入数据源到 UI
    setDataBuffer(m_parseWorker->buffer());
    m_historyPlayer->setDbPath("data.db");
    m_historyPlayer->setDataBuffer(m_parseWorker->buffer());
    m_historyPlayer->loadTimeRange();
    // ⑥ 启动线程
    m_commThread->start();
    m_parseThread->start();

    // ⑦ 让通信线程打开 TCP
    QMetaObject::invokeMethod(m_tcpWorker, "open", Qt::QueuedConnection);

    m_statusLabel->setText("连接中...");
}

void MainWindow::onDisconnect()
{
    m_collecting = false;
    if (!m_commThread) return;

    setDataBuffer(nullptr);

    // ① 让通信线程自己关 socket（用 invokeMethod，不是直接调）
    QMetaObject::invokeMethod(m_tcpWorker, "close", Qt::QueuedConnection);

    // ② Worker 用 deleteLater —— 在自己的线程里自杀
    m_tcpWorker->deleteLater();
    m_parseWorker->deleteLater();

    // ③ 线程退出
    m_commThread->quit();
    m_parseThread->quit();
    m_commThread->wait();
    m_parseThread->wait();

    m_tcpWorker   = nullptr;
    m_parseWorker = nullptr;
    delete m_commThread;  m_commThread  = nullptr;
    delete m_parseThread; m_parseThread = nullptr;

    m_statusLabel->setText("已断开");
}

void MainWindow::onStart()
{
    if (!m_commThread) onConnect();
    if (m_parseWorker)
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting", Qt::QueuedConnection, Q_ARG(bool, true));
    m_statusLabel->setText("采集中...");
}

void MainWindow::onStop()
{
    if (m_parseWorker)
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting", Qt::QueuedConnection, Q_ARG(bool, false));
    m_statusLabel->setText("已暂停");
}


