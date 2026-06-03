//==============================================================================
// PulseQt - 多通道数据采集上位机
// 应用入口：初始化日志系统，启动主窗口
//==============================================================================

#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Logger::instance().init("app.log");
    qInfo() << "PulseQt started";

    MainWindow window;
    window.show();

    return app.exec();
}
