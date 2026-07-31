#include <QApplication>
#include <QFile>
#include <QIcon>

#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Kill");
    QApplication::setOrganizationName("KillLauncher");
    QApplication::setWindowIcon(QIcon(":/icon.ico"));

    QFile styleFile(":/assets/style.qss");
    if (styleFile.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    MainWindow window;
    window.show();
    return app.exec();
}
