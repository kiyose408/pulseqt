//==============================================================================
// MainWindow - 主窗口
//
// T009: 集成 DataTableModel + QTableView（数据表格）
// T010: RealTimeChart（左侧自绘曲线）
// T011: QSplitter 布局 + 工具栏 + 状态栏 + 菜单栏
//==============================================================================

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableView>
#include "DataTableModel.h"
#include "DataBuffer.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    // 设置数据源（T009 引入）
    void setDataBuffer(DataBuffer *buffer);

private:
    QTableView     *m_tableView  = nullptr;
    DataTableModel *m_tableModel = nullptr;
};

#endif // MAINWINDOW_H
