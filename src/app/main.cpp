#include <QApplication>
#include <QIcon>
#include <QString>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LDirStat");
    QApplication::setDesktopFileName("ldirstat");
    QApplication::setApplicationVersion(QString::fromLatin1(LDIRSTAT_APP_VERSION));
    QApplication::setWindowIcon(QIcon(":/icons/app.svg"));

    ldirstat::MainWindow window;
    window.show();

    return QApplication::exec();
}
