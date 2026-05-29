#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    //QApplication 之后立即初始化日志
    Logger::instance().init("app.log");
    qInfo() << "PulseQt started";

    MainWindow window;
    window.show();
    return app.exec();

}
