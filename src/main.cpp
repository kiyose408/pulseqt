//==============================================================================
// PulseQt - 多通道数据采集上位机
//==============================================================================

#include <QApplication>
#include <QMessageBox>
#include <QThread>
#include "MainWindow.h"
#include "Logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Logger::instance().init("app.log");

    // 注册 GUI 错误弹窗回调（ERROR / FATAL 级弹出 QMessageBox）
    Logger::instance().setErrorCallback([](const QString &msg) {
        // QMessageBox 必须在主线程创建 → 非主线程时用 invokeMethod 投递
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            QMessageBox::critical(nullptr, "PulseQt Error", msg);
        else
            QMetaObject::invokeMethod(QCoreApplication::instance(), [msg]() {
                QMessageBox::critical(nullptr, "PulseQt Error", msg);
            }, Qt::QueuedConnection);
    });

    qInfo() << "PulseQt started";
    MainWindow window;
    window.show();
    return app.exec();
}
