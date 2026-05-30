//==============================================================================
// MainWindow - 主窗口
//
// 当前为骨架实现（T001），后续任务会逐步集成：
//   T009: DataTableModel + QTableView（右侧数据表格）
//   T010: RealTimeChart（左侧自绘曲线）
//   T011: QSplitter 布局 + 工具栏 + 状态栏 + 菜单栏
//==============================================================================

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;
};

#endif // MAINWINDOW_H
