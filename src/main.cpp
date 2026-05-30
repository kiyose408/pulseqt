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
