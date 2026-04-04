#include <cstdlib>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include "mainwindow.h"

namespace {

QString debugLogPath() {
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (logDir.isEmpty()) logDir = QDir(QDir::tempPath()).filePath(QStringLiteral("LDirStat"));

    QDir dir(logDir);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("debug.log"));
}

QString messageTypeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("CRIT");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("LOG");
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static QMutex mutex;

    const QMutexLocker locker(&mutex);
    QFile file(debugLogPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' '
               << messageTypeName(type) << ' ';
        if (context.category && context.category[0] != '\0') stream << '[' << context.category << "] ";
        stream << message;
        if (context.file && context.line > 0) stream << " (" << context.file << ':' << context.line << ')';
        stream << '\n';
        stream.flush();
    }

    if (type == QtFatalMsg) std::abort();
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LDirStat");
    QApplication::setDesktopFileName("ldirstat");
    QApplication::setApplicationVersion(QString::fromLatin1(LDIRSTAT_APP_VERSION));
    QApplication::setWindowIcon(QIcon(":/icons/app.svg"));
    qInstallMessageHandler(fileMessageHandler);

    ldirstat::MainWindow window;
    window.show();

    return QApplication::exec();
}
