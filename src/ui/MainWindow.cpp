//==============================================================================
// MainWindow 实现（骨架）
//==============================================================================

#include "MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)   // 初始化列表：先构造基类 QMainWindow
{
    setWindowTitle("PulseQt");   // 窗口标题
    resize(1200, 800);           // 初始大小（宽 × 高）
}
