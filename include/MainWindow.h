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
#include "DataTableModel.h"
#include "RealTimeChart.h"
#include "DataBuffer.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

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

private slots:
    void onExportCsv();
    void onAbout();
    void onConnect();
    void onDisconnect();
};

#endif // MAINWINDOW_H
