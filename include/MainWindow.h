//==============================================================================
// MainWindow - 主窗口
//
// T009: 集成 DataTableModel + QTableView（右侧数据表格）
// T010: RealTimeChart（左侧自绘曲线）
// T011: QSplitter 布局 + 工具栏 + 状态栏 + 菜单栏
//==============================================================================

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableView>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QSplitter>
#include <QThread>
#include <QVBoxLayout>
#include "DataTableModel.h"
#include "RealTimeChart.h"
#include "DataBuffer.h"
#include "TcpWorker.h"        // 替代 ChannelManager.h
#include "ParseWorker.h"      // 替代 ProtocolDecoder.h + DatabaseManager.h
#include "HistoryPlayer.h"


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

    void setDataBuffer(DataBuffer *buffer);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralArea();
    void setupStatusBar();

    RealTimeChart  *m_chart       = nullptr;
    QTableView     *m_tableView   = nullptr;
    DataTableModel *m_tableModel  = nullptr;
    QLabel         *m_statusLabel = nullptr;

    bool m_collecting = false;   // 开始/停止采集标记

    QThread     *m_commThread  = nullptr;   // 通信线程
    QThread     *m_parseThread = nullptr;   // 解析线程
    TcpWorker   *m_tcpWorker   = nullptr;   // TCP 收发
    ParseWorker *m_parseWorker = nullptr;   // 解码 + 缓冲 + DB
    HistoryPlayer *m_historyPlayer = nullptr;

private slots:
    void onExportCsv();
    void onAbout();
    void onConnect();
    void onDisconnect();
    void onStart();
    void onStop();
};

#endif // MAINWINDOW_H
