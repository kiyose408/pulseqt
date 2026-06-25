//==============================================================================
// PulseQt - 多通道数据采集上位机
//==============================================================================

#include <QApplication>
#include "MagMainWindow.h"
#include "Logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Logger::instance().init("app.log");
    qInfo() << "PulseQt v2.0 started";
    MagMainWindow window;
    window.show();
    return app.exec();
}
