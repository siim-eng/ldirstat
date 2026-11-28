#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QTimer>

#include "mainwindow.h"

namespace {

QString debugLogPath() {
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (logDir.isEmpty()) logDir = QDir(QDir::tempPath()).filePath(QStringLiteral("LDirStat"));

    // Fall back to the temp directory itself if the log directory cannot be created.
    if (!QDir().mkpath(logDir)) logDir = QDir::tempPath();
    return QDir(logDir).filePath(QStringLiteral("debug.log"));
}

QString messageTypeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("CRIT");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }

    return QStringLiteral("LOG");
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static QMutex mutex;

    const QMutexLocker locker(&mutex);
    QFile file(debugLogPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' ' << messageTypeName(type) << ' ';
        if (context.category && context.category[0] != '\0') stream << '[' << context.category << "] ";
        stream << message;
        if (context.file && context.line > 0) stream << " (" << context.file << ':' << context.line << ')';
        stream << '\n';
        stream.flush();
    }

    if (type == QtFatalMsg) std::abort();
}

void printError(const QString &message) {
    std::fprintf(stderr, "LDirStat: %s\n", qPrintable(message));
}

// Returns the canonical path of arg, or an empty string after printing why it can't be scanned.
QString checkedScanDirectory(const QString &arg) {
    const QFileInfo info(arg);
    if (!info.exists()) {
        printError(QStringLiteral("%1: no such file or directory").arg(arg));
        return {};
    }
    if (!info.isDir()) {
        printError(QStringLiteral("%1: not a directory").arg(arg));
        return {};
    }
    return info.canonicalFilePath();
}

// QApplication aborts when it can't reach a display. --help and --version only print text,
// so they run on a QCoreApplication instead and work over ssh or in scripts.
bool requestsTextOnly(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--") break;
        if (arg == "-h" || arg == "-?" || arg == "--help" || arg == "--help-all" || arg == "-v" || arg == "--version")
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[]) {
    // A lone plain argument is the directory: check it before QApplication needs a display.
    // Anything with options (including Qt's own, such as -platform) is checked after parsing.
    if (argc == 2 && argv[1][0] != '-' && checkedScanDirectory(QString::fromLocal8Bit(argv[1])).isEmpty()) return EXIT_FAILURE;

    const bool textOnly = requestsTextOnly(argc, argv);
    std::unique_ptr<QCoreApplication> app;
    if (textOnly)
        app = std::make_unique<QCoreApplication>(argc, argv);
    else
        app = std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName("LDirStat");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(LDIRSTAT_APP_VERSION));
    if (!textOnly) {
        QGuiApplication::setDesktopFileName("ldirstat");
        QGuiApplication::setWindowIcon(QIcon(":/icons/app.svg"));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Analyze disk usage statistics by directory."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("directory"),
                                 QStringLiteral("Directory to scan on startup."),
                                 QStringLiteral("[directory]"));
    parser.process(*app);
    if (textOnly) return EXIT_SUCCESS; // process() has already printed help or version and exited

    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) {
        printError(QStringLiteral("too many arguments; expected at most one directory"));
        return EXIT_FAILURE;
    }

    QString scanPath;
    if (!positional.isEmpty()) {
        scanPath = checkedScanDirectory(positional.first());
        if (scanPath.isEmpty()) return EXIT_FAILURE;
    }

    qInstallMessageHandler(fileMessageHandler);

    ldirstat::MainWindow window;
    window.show();
    if (!scanPath.isEmpty()) QTimer::singleShot(0, &window, [&window, scanPath]() { window.scanDirectory(scanPath); });

    return QApplication::exec();
}
