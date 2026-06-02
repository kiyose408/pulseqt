//==============================================================================
// PulseQt - 多通道数据采集上位机
// 应用入口：初始化日志系统，启动主窗口
//==============================================================================

#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"

int main(int argc, char *argv[])
{
    // Qt 应用程序对象（必须在所有窗口对象之前创建）
    QApplication app(argc, argv);

    // 初始化分级日志系统（qInstallMessageHandler）
    Logger::instance().init("app.log");
    qInfo() << "PulseQt started";

    // 创建并显示主窗口（栈上创建，事件循环退出前不会销毁）
    MainWindow window;
    window.show();

    // 进入 Qt 事件循环，阻塞直到窗口关闭
    return app.exec();
}