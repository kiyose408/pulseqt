//==============================================================================
// MainWindow 实现
//==============================================================================

#include "MainWindow.h"
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PulseQt");
    resize(1200, 800);

    // ── 创建表格模型 ────────────────────────────────────────────
    m_tableModel = new DataTableModel(this);

    // ── 创建表格视图 ────────────────────────────────────────────
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_tableModel);

    // 样式：交替行颜色 + 自动调整列宽 + 不可编辑
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 模型刷新 → 表格自动滚到底部
    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });

    // 暂时占满整个窗口（T011 改为 QSplitter 分左右）
    setCentralWidget(m_tableView);
}

void MainWindow::setDataBuffer(DataBuffer *buffer)
{
    m_tableModel->setDataBuffer(buffer);
}
