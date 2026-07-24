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
#include <QAction>
#include "DataTableModel.h"
#include "RealTimeChart.h"
#include "DataBuffer.h"
#include "ChannelManager.h"
#include "ChannelRegistry.h"
#include "ConnectionDialog.h"
#include "ParseWorker.h"      // 解码 + 缓冲 + DB
#include "HistoryPlayer.h"
#include "AlarmPanel.h"


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

    void setDataBuffer(DataBuffer *buffer);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralArea();
    void setupStatusBar();

    RealTimeChart  *m_chart       = nullptr;   // 实时曲线
    RealTimeChart  *m_playbackChart = nullptr;  // 回放曲线（独立 buffer）
    QTableView     *m_tableView   = nullptr;
    DataTableModel *m_tableModel  = nullptr;
    QLabel         *m_statusLabel = nullptr;

    bool m_collecting = false;   // 开始/停止采集标记
    bool m_connected  = false;   // 通道已连接（≠ 线程存在）
    bool m_darkTheme  = false;   // 暗色主题

    QThread        *m_commThread     = nullptr;   // 通信线程
    QThread        *m_parseThread    = nullptr;   // 解析线程
    ChannelManager *m_channelManager = nullptr;   // 通道管理（重连 + 转发）
    ParseWorker    *m_parseWorker    = nullptr;   // 解码 + 缓冲 + DB + 管道
    HistoryPlayer  *m_historyPlayer  = nullptr;
    QAction        *m_filterAction   = nullptr;   // 过滤器管道开关
    AlarmPanel     *m_alarmPanel     = nullptr;   // 告警面板
    QDockWidget    *m_chartDock      = nullptr;   // 实时曲线面板
    QDockWidget    *m_tableDock      = nullptr;   // 数据表格面板
    QDockWidget    *m_playbackDock   = nullptr;   // 回放面板
    QDockWidget    *m_alarmDock      = nullptr;   // 告警面板容器

private slots:
    void onExportCsv();
    void onAbout();
    void onConnect();
    void onDisconnect();
    void teardown();              // 全部拆光（仅 closeEvent 调用）
    void toggleTheme();           // 切换暗色/亮色主题
    void restoreDefaultLayout();   // 恢复 Dock 默认布局
    void savePipelineConfig();
    void restorePipelineConfig();
    void onStart();
    void onStop();
    void refreshAlarmConnection();
};

#endif // MAINWINDOW_H
