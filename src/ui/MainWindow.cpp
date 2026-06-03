//==============================================================================
// MainWindow 实现
//==============================================================================

#include "MainWindow.h"
#include <QHeaderView>
#include <QHBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PulseQt");
    resize(1200, 800);

    // ── 左侧：自绘曲线 ──────────────────────────────────────────
    m_chart = new RealTimeChart(this);

    // ── 右侧：数据表格 ──────────────────────────────────────────
    m_tableModel = new DataTableModel(this);
    m_tableView  = new QTableView(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_tableModel, &DataTableModel::dataRefreshed, this, [this]() {
        m_tableView->scrollToBottom();
    });

    // ── 临时水平布局（T011 改为 QSplitter）────────────────────
    QWidget *central = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chart, 7);        // 左侧占 70%
    layout->addWidget(m_tableView, 3);    // 右侧占 30%
    setCentralWidget(central);
}

void MainWindow::setDataBuffer(DataBuffer *buffer)
{
    m_tableModel->setDataBuffer(buffer);
    m_chart->setDataBuffer(buffer);       // 同一个 DataBuffer，曲线和表格共享
}
