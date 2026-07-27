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
#include "FilterConfigDialog.h"
#include "ThresholdAlarm.h"
#include "MovingAverageFilter.h"
#include "MedianFilter.h"
#include "AlarmPanel.h"
#include "ThresholdAlarm.h"
#include <QDockWidget>
#include <QSettings>
#include "SpectrumWidget.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PulseQt");

    setupCentralArea();   // 先创建曲线+表格（setupMenuBar 可能引用）
    setupMenuBar();
    setupToolBar();
    setupStatusBar();

    // 恢复窗口状态
    QSettings settings;
    if (!restoreGeometry(settings.value("window/geometry").toByteArray()))
        resize(1200, 800);   // 首次运行用默认大小
    restoreState(settings.value("window/dockState").toByteArray(), 2);
    m_darkTheme = settings.value("window/darkTheme", false).toBool();
    if (m_darkTheme) toggleTheme();
    double tw = settings.value("window/timeWindow", 30.0).toDouble();
    if (m_chart) m_chart->setTimeWindow(tw);
}

MainWindow::~MainWindow()
{
    teardown();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/dockState", saveState(2));
    settings.setValue("window/darkTheme", m_darkTheme);
    if (m_chart) settings.setValue("window/timeWindow", m_chart->timeWindow());
    savePipelineConfig();

    teardown();
    event->accept();
}

void MainWindow::setDataBuffer(DataBuffer *buffer)
{
    m_tableModel->setDataBuffer(buffer);
    m_chart->setDataBuffer(buffer);
    m_spectrumWidget->setDataBuffer(buffer);
}

//==============================================================================
// 菜单栏
//==============================================================================

void MainWindow::setupMenuBar()
{
    // ── 文件 ──
    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("导出 CSV..."), this, &MainWindow::onExportCsv);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&Q)"), this, &QWidget::close);

    // ── 视图 ──
    QMenu *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("显示表格"));
    viewMenu->addAction(tr("暗色主题"), this, &MainWindow::toggleTheme);
    m_filterAction = viewMenu->addAction(tr("过滤器管道"));
    m_filterAction->setCheckable(true);
    m_filterAction->setChecked(true);   // 默认启用（空管道直通）
    connect(m_filterAction, &QAction::toggled, this, [this](bool on) {
        if (m_parseWorker)
            QMetaObject::invokeMethod(m_parseWorker->pipeline(), "setEnabled",
                                      Qt::QueuedConnection, Q_ARG(bool, on));
    });
    viewMenu->addSeparator();
    viewMenu->addAction(tr("配置过滤器..."), this, [this]() {
        if (m_parseWorker) {
            FilterConfigDialog dlg(m_parseWorker->pipeline(), this);
            dlg.exec();
            int n = m_parseWorker->pipeline()->count();
            m_statusLabel->setText(n > 0
                ? QString("过滤器管道: %1 个").arg(n)
                : QString("过滤器管道: 空"));
            refreshAlarmConnection();  // 重建告警信号连接
        }
    });

    // ── 面板显隐 ──
    viewMenu->addSeparator();
    auto addDockToggle = [&](const QString &title, QDockWidget *dock) {
        QAction *a = viewMenu->addAction(title);
        a->setCheckable(true);
        a->setChecked(true);
        connect(a, &QAction::toggled, dock, &QDockWidget::setVisible);
        connect(dock, &QDockWidget::visibilityChanged, a, &QAction::setChecked);
    };
    addDockToggle(tr("数据表格"), m_tableDock);
    addDockToggle(tr("历史回放"), m_playbackDock);
    addDockToggle(tr("告警面板"), m_alarmDock);
    addDockToggle(tr("频谱分析"), m_spectrumDock);

    viewMenu->addSeparator();
    viewMenu->addAction(tr("恢复默认布局"), this, [this]() {
        restoreDefaultLayout();
    });

    viewMenu->addSeparator();
    QAction *langAction = viewMenu->addAction("English");
    langAction->setCheckable(true);
    QSettings settings;
    langAction->setChecked(settings.value("window/language") == "en");
    connect(langAction, &QAction::toggled, this, [](bool en) {
        QSettings s;
        s.setValue("window/language", en ? "en" : "zh");
        QMessageBox::information(nullptr, "Language", "Restart PulseQt to apply.");
    });

    // ── 帮助 ──
    QMenu *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于..."), this, &MainWindow::onAbout);
}

//==============================================================================
// 工具栏
//==============================================================================

void MainWindow::setupToolBar()
{
    QToolBar *tb = addToolBar("主工具栏");
    tb->addAction(tr("连接"), this, &MainWindow::onConnect);
    tb->addAction(tr("断开"), this, &MainWindow::onDisconnect);
    tb->addSeparator();
    tb->addAction(tr("开始"), this, &MainWindow::onStart);
    tb->addAction(tr("停止"), this, &MainWindow::onStop);
}

//==============================================================================
// 中央区域 — QSplitter 左右分屏
//==============================================================================

void MainWindow::setupCentralArea()
{
    // ── 实时曲线 ──
    m_chart = new RealTimeChart(this);
    m_chartDock = new QDockWidget("实时曲线", this);
    m_chartDock->setWidget(m_chart);
    m_chartDock->setObjectName("dockChart");

    // ── 数据表格 ──
    m_tableModel = new DataTableModel(this);
    m_tableView  = new QTableView(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });
    m_tableDock = new QDockWidget(tr("数据表格"), this);
    m_tableDock->setWidget(m_tableView);
    m_tableDock->setObjectName("dockTable");

    // ── 回放曲线 + 进度条（同一个 Dock） ──
    m_playbackChart = new RealTimeChart(this);
    m_playbackChart->setMinimumHeight(100);
    m_historyPlayer = new HistoryPlayer(this);
    m_historyPlayer->setMaximumHeight(50);
    QWidget *playbackWidget = new QWidget(this);
    QVBoxLayout *pbLayout = new QVBoxLayout(playbackWidget);
    pbLayout->setContentsMargins(0, 0, 0, 0);
    pbLayout->addWidget(m_playbackChart, 1);
    pbLayout->addWidget(m_historyPlayer);
    m_playbackDock = new QDockWidget(tr("历史回放"), this);
    m_playbackDock->setWidget(playbackWidget);
    m_playbackDock->setObjectName("dockPlayback");

    // ── 告警面板 ──
    m_alarmPanel = new AlarmPanel(this);
    m_alarmDock = new QDockWidget("告警", this);
    m_alarmDock->setWidget(m_alarmPanel);
    m_alarmDock->setObjectName("dockAlarm");

    // ── 频谱分析 ──
    m_spectrumWidget = new SpectrumWidget(this);
    m_spectrumWidget->setFFTSize(256);
    m_spectrumDock = new QDockWidget(tr("频谱分析"), this);
    m_spectrumDock->setWidget(m_spectrumWidget);
    m_spectrumDock->setObjectName("dockSpectrum");

    // ── 添加所有 Dock ──
    addDockWidget(Qt::LeftDockWidgetArea,  m_chartDock);
    splitDockWidget(m_chartDock, m_spectrumDock, Qt::Horizontal);
    splitDockWidget(m_spectrumDock, m_alarmDock, Qt::Vertical);
    splitDockWidget(m_chartDock, m_playbackDock, Qt::Vertical);
    splitDockWidget(m_playbackDock, m_tableDock, Qt::Horizontal);

    // ── Dock 属性 ──
    m_chartDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable);
    for (auto *d : {m_tableDock, m_playbackDock, m_alarmDock, m_spectrumDock}) {
        d->setFeatures(QDockWidget::DockWidgetMovable |
                       QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
    }
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

    // ── 弹对话框 ──
    QSettings settings;
    QString lastChannel = settings.value("connection/lastChannel").toString();
    QVariantMap lastCfg = settings.value("connection/lastConfig").toMap();

    ConnectionDialog dlg(this);
    if (!lastChannel.isEmpty())
        dlg.setInitialValues(lastChannel, lastCfg);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QString    channelId = dlg.selectedChannelId();
    QVariantMap cfg      = dlg.config();

    // 保存本次配置
    settings.setValue("connection/lastChannel", channelId);
    settings.setValue("connection/lastConfig", cfg);

    // ── 首次连接：创建线程 + 管理器 + 解析器 ──────────
    if (!m_parseWorker) {
        m_commThread     = new QThread(this);
        m_parseThread    = new QThread(this);
        m_channelManager = new ChannelManager();
        m_parseWorker = new ParseWorker("data.db");

        m_channelManager->moveToThread(m_commThread);
        m_parseWorker->moveToThread(m_parseThread);

        connect(m_channelManager, &ChannelManager::readyRead,
                m_parseWorker, &ParseWorker::onRawDataReceived,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::writeData,
                m_channelManager, &ChannelManager::writeData,
                Qt::QueuedConnection);
        connect(m_parseWorker, &ParseWorker::dataPointReady,
                this, [this]() {
            // 数据到达 → 触发曲线和表格即时刷新
            if (m_chart) m_chart->update();
        }, Qt::QueuedConnection);
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
    restorePipelineConfig();

        m_commThread->start();
        m_parseThread->start();
    }

    // ── 每次连接都切换协议 ─────
    {
        QString protoArg = (cfg.value("protocol", "自定义").toString() == "Modbus RTU")
                            ? QString("modbus") : QString("raw");
        QMetaObject::invokeMethod(m_parseWorker, "setProtocol", Qt::QueuedConnection,
                                  Q_ARG(QString, protoArg));
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
        QMetaObject::invokeMethod(m_parseWorker, "setCollecting",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(bool, false));
        QMetaObject::invokeMethod(m_parseWorker, "teardown",
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_parseWorker, "resetChannelConfig",
                                  Qt::BlockingQueuedConnection);
    }

    // 调度在所属线程安全析构（避免 socket 跨线程清理）
    if (m_channelManager) { m_channelManager->deleteLater(); m_channelManager = nullptr; }
    if (m_parseWorker)    { m_parseWorker->deleteLater();    m_parseWorker    = nullptr; }

    if (m_commThread && m_commThread->isRunning()) {
        m_commThread->quit();
        m_commThread->wait(5000);
    }
    if (m_parseThread && m_parseThread->isRunning()) {
        m_parseThread->quit();
        m_parseThread->wait(5000);
    }

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



void MainWindow::refreshAlarmConnection()
{
    if (!m_parseWorker || !m_alarmPanel) return;

    // 查找管道中的 ThresholdAlarm，连接信号到告警面板
    auto *pipeline = m_parseWorker->pipeline();
    for (int i = 0; i < pipeline->count(); ++i) {
        auto *f = pipeline->filterAt(i);
        if (!f || f->name() != "ThresholdAlarm") continue;
        auto *alarm = static_cast<ThresholdAlarm*>(f);

        // UI 面板连接（QueuedConnection → UI 线程）
        connect(alarm, &ThresholdAlarm::alarmTriggered,
                m_alarmPanel, &AlarmPanel::onAlarmTriggered,
                Qt::QueuedConnection);
        connect(alarm, &ThresholdAlarm::alarmCleared,
                m_alarmPanel, &AlarmPanel::onAlarmCleared,
                Qt::QueuedConnection);

        // DB 写入连接（DirectConnection → 同线程 ParseWorker，安全）
        auto *db = m_parseWorker->dbManager();
        connect(alarm, &ThresholdAlarm::alarmTriggered, this,
                [db](int ch, double val, double th, bool upper) {
                    db->insertAlarm(ch, val, th, upper, "triggered");
                }, Qt::DirectConnection);
        connect(alarm, &ThresholdAlarm::alarmCleared, this,
                [db](int ch) {
                    db->insertAlarm(ch, 0, 0, false, "cleared");
                }, Qt::DirectConnection);
        break;
    }
}

void MainWindow::restoreDefaultLayout()
{
    // 移除所有 dock 后重新添加，恢复初始布局
    removeDockWidget(m_tableDock);
    removeDockWidget(m_playbackDock);
    removeDockWidget(m_alarmDock);
    removeDockWidget(m_spectrumDock);

    addDockWidget(Qt::LeftDockWidgetArea, m_chartDock);
    splitDockWidget(m_chartDock, m_spectrumDock, Qt::Horizontal);
    splitDockWidget(m_chartDock, m_playbackDock, Qt::Vertical);
    splitDockWidget(m_spectrumDock, m_alarmDock, Qt::Vertical);
    splitDockWidget(m_playbackDock, m_tableDock, Qt::Horizontal);

    m_playbackDock->show();
    m_alarmDock->show();
    m_spectrumDock->show();
    m_tableDock->show();
}

void MainWindow::savePipelineConfig()
{
    if (!m_parseWorker) return;
    QSettings settings;
    QStringList descs;
    auto *pipe = m_parseWorker->pipeline();
    for (int i = 0; i < pipe->count(); ++i) {
        auto *f = pipe->filterAt(i);
        if (!f) continue;
        QString d = f->name();
        auto chs = f->channels();
        if (!chs.isEmpty()) {
            QStringList sl; for (int c : chs) sl << QString::number(c);
            d += "::" + sl.join(",");
        }
        descs << d;
    }
    settings.setValue("pipeline/filters", descs);
}

void MainWindow::restorePipelineConfig()
{
    if (!m_parseWorker) return;
    QSettings settings;
    QStringList saved = settings.value("pipeline/filters").toStringList();
    for (const QString &desc : saved) {
        auto parts = desc.split("::");
        QString name = parts.value(0);
        QVector<int> chs;
        if (parts.size() > 1) {
            for (const QString &s : parts[1].split(',')) {
                bool ok; int c = s.trimmed().toInt(&ok);
                if (ok) chs.append(c);
            }
        }
        std::unique_ptr<IFilter> f;
        int win = 5;
        if (name.startsWith("MovingAverage")) {
            f = std::make_unique<MovingAverageFilter>(win,
                name.contains("EMA") ? MovingAverageFilter::EMA : MovingAverageFilter::Simple);
        } else if (name.startsWith("Median")) {
            f = std::make_unique<MedianFilter>(win);
        } else if (name == "ThresholdAlarm") {
            auto *a = new ThresholdAlarm;
            a->setUpperLimit(0, settings.value("alarm/upper", 900).toDouble());
            a->setLowerLimit(0, settings.value("alarm/lower", 100).toDouble());
            a->setHysteresis(settings.value("alarm/hysteresis", 5.0).toDouble());
            f.reset(a);
        }
        if (f) {
            if (!chs.isEmpty()) f->setChannels(chs);
            m_parseWorker->pipeline()->addFilter(std::move(f));
        }
    }
}
