#include <QApplication>
#include <QIcon>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("LDirStat");
    app.setWindowIcon(QIcon(":/icons/app.svg"));

    ldirstat::MainWindow window;
    window.show();

    return app.exec();
}
