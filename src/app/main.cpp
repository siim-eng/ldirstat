#include <QApplication>
#include <QIcon>
#include <QString>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("LDirStat");
    app.setDesktopFileName("ldirstat");
    app.setApplicationVersion(QString::fromLatin1(LDIRSTAT_APP_VERSION));
    app.setWindowIcon(QIcon(":/icons/app.svg"));

    ldirstat::MainWindow window;
    window.show();

    return app.exec();
}
