#include "mainwindow.h"
#include "mainwindowbuilder.h"

#include "dirlistview.h"
#include "entrytooltip.h"
#include "graphwidget.h"
#include "scanprogresswidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

namespace ldirstat {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , scanner_(entryStore_, nameStore_) {
    MainWindowBuilder::build(this);
    qApp->installEventFilter(this);

    themeColors_ = ThemeColors::fromPalette(palette());
    dirListView_->setThemeColors(themeColors_);
    if (graphTypeStack_) {
        for (int i = 0; i < graphTypeStack_->count(); ++i) {
            auto* widget = qobject_cast<GraphWidget*>(graphTypeStack_->widget(i));
            if (widget)
                widget->setThemeColors(themeColors_);
        }
    }

    refreshWelcomeVolumes();

    connect(this, &MainWindow::scanComplete,
            this, &MainWindow::onScanFinished, Qt::QueuedConnection);
}

MainWindow::~MainWindow() {
    if (qApp)
        qApp->removeEventFilter(this);

    if (mountProcess_) {
        mountProcess_->kill();
        mountProcess_->waitForFinished();
        delete mountProcess_;
    }

    if (scanThread_) {
        scanThread_->wait();
        delete scanThread_;
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        themeColors_ = ThemeColors::fromPalette(palette());
        dirListView_->setThemeColors(themeColors_);
        if (graphTypeStack_) {
            for (int i = 0; i < graphTypeStack_->count(); ++i) {
                auto* widget = qobject_cast<GraphWidget*>(graphTypeStack_->widget(i));
                if (widget)
                    widget->setThemeColors(themeColors_);
            }
        }
    }
    QMainWindow::changeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (shouldForwardDirListArrowKey(watched, event)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (dirListView_->handleArrowKey(keyEvent->key()))
            return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshWelcomeVolumes() {
    fileSystems_.refresh();
    welcomeWidget_->populate(fileSystems_);
}

void MainWindow::setMountInProgress(bool inProgress, const QString& status) {
    if (welcomeWidget_)
        welcomeWidget_->setBusy(inProgress, status);
    if (toolbar_)
        toolbar_->setEnabled(!inProgress);

    if (inProgress) {
        if (!QApplication::overrideCursor())
            QApplication::setOverrideCursor(Qt::BusyCursor);
        return;
    }

    if (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
}

bool MainWindow::shouldForwardDirListArrowKey(QObject* watched, QEvent* event) const {
    if (!dirListView_ || event->type() != QEvent::KeyPress)
        return false;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers() != Qt::NoModifier)
        return false;

    switch (keyEvent->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
        break;
    default:
        return false;
    }

    if (!viewStack_ || !flameStack_ || !graphTypeStack_)
        return false;
    if (viewStack_->currentWidget() == welcomeWidget_)
        return false;
    if (flameStack_->currentWidget() != graphTypeStack_)
        return false;
    if (!dirListView_->isEnabled() || !dirListView_->isVisible())
        return false;
    if (QApplication::activePopupWidget())
        return false;

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget)
        return false;

    return widget->window() == this;
}

void MainWindow::onOverview() {
    if (mountProcess_)
        return;

    if (viewStack_->currentIndex() == 1) {
        refreshWelcomeVolumes();
        viewStack_->setCurrentIndex(0);
        overviewAction_->setText(tr("Back"));
        rescanAction_->setVisible(false);
    } else if (currentRoot_.valid()) {
        viewStack_->setCurrentIndex(1);
        overviewAction_->setText(tr("Overview"));
        rescanAction_->setVisible(true);
    }
}

void MainWindow::onRescan() {
    if (!lastScanPath_.isEmpty())
        startScan(lastScanPath_);
}

void MainWindow::startScan(const QString& path) {
    if (mountProcess_ || (scanThread_ && scanThread_->isRunning()))
        return;

    lastScanPath_ = path;

    toolbar_->setVisible(true);
    overviewAction_->setText(tr("Overview"));
    rescanAction_->setVisible(true);

    viewStack_->setCurrentIndex(1);
    flameStack_->setCurrentIndex(0);
    scanProgress_->reset();
    scanPollTimer_->start();
    dirListView_->setEnabled(false);

    delete scanThread_;
    scanThread_ = nullptr;

    currentRoot_ = kNoEntry;
    selectedDir_ = kNoEntry;

    entryStore_.clear();
    nameStore_.clear();
    if (graphTypeStack_) {
        for (int i = 0; i < graphTypeStack_->count(); ++i) {
            auto* widget = qobject_cast<GraphWidget*>(graphTypeStack_->widget(i));
            if (!widget)
                continue;
            widget->setStores(nullptr, nullptr);
            widget->setDirectory(kNoEntry);
        }
    }

    auto scanPath = path.toStdString();
    int workers = QThread::idealThreadCount();

    scanThread_ = QThread::create([this, scanPath, workers]() {
        EntryRef root = scanner_.scan(scanPath, workers);
        scanner_.propagate(root);
        scanner_.sortBySize(workers);
        emit scanComplete(root);
    });
    scanThread_->start();
}

void MainWindow::mountAndScan(const QString& devicePath) {
    if (devicePath.isEmpty() || mountProcess_ || (scanThread_ && scanThread_->isRunning()))
        return;

    viewStack_->setCurrentIndex(0);
    setMountInProgress(true, tr("Mounting %1...").arg(devicePath));

    auto* process = new QProcess(this);
    mountProcess_ = process;
    process->setProgram(QStringLiteral("udisksctl"));
    process->setArguments({QStringLiteral("mount"), QStringLiteral("-b"), devicePath});

    const auto complete = [this, process, devicePath](bool success, const QString& message) {
        if (process != mountProcess_)
            return;

        mountProcess_ = nullptr;
        refreshWelcomeVolumes();
        setMountInProgress(false);
        process->deleteLater();

        if (!success) {
            QMessageBox::warning(this,
                                 tr("Mount Failed"),
                                 message.isEmpty()
                                     ? tr("Unable to mount %1.").arg(devicePath)
                                     : message);
            return;
        }

        const VolumeInfo* volume =
            fileSystems_.findVolumeByDevice(devicePath.toStdString());
        if (!volume || !volume->mounted || volume->mountPoint.empty()) {
            QMessageBox::warning(this,
                                 tr("Mount Failed"),
                                 tr("Mounted %1, but no mount point could be resolved.")
                                     .arg(devicePath));
            return;
        }

        startScan(QString::fromStdString(volume->mountPoint));
    };

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [process, complete](int exitCode, QProcess::ExitStatus exitStatus) {
                const QString stdErr =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                const QString stdOut =
                    QString::fromUtf8(process->readAllStandardOutput()).trimmed();

                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    const QString message =
                        !stdErr.isEmpty() ? stdErr
                                          : (!stdOut.isEmpty() ? stdOut : process->errorString());
                    complete(false, message);
                    return;
                }

                complete(true, {});
            });

    connect(process,
            &QProcess::errorOccurred,
            this,
            [process, complete](QProcess::ProcessError) {
                const QString message =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                complete(false, message.isEmpty() ? process->errorString() : message);
            });

    process->start();
}

void MainWindow::onScanPollTick() {
    scanProgress_->updateCounts(scanner_.filesScanned(), scanner_.dirsScanned());
}

void MainWindow::onStopScan() {
    scanner_.stop();
}

void MainWindow::onScanFinished(EntryRef root) {
    scanPollTimer_->stop();

    if (scanThread_) {
        scanThread_->wait();
        delete scanThread_;
        scanThread_ = nullptr;
    }

    if (scanner_.stopped()) {
        entryStore_.clear();
        nameStore_.clear();
        dirListView_->setEnabled(true);
        viewStack_->setCurrentIndex(0);
        refreshWelcomeVolumes();
        if (!currentRoot_.valid()) {
            toolbar_->setVisible(false);
        } else {
            overviewAction_->setText(tr("Back"));
            rescanAction_->setVisible(false);
        }
        return;
    }

    onScanPollTick();
    flameStack_->setCurrentIndex(1);
    dirListView_->setEnabled(true);

    currentRoot_ = root;
    selectedDir_ = root;

    dirListView_->setRoot(entryStore_, nameStore_, root);
    graphWidget_->setStores(&entryStore_, &nameStore_);
    onDirSelected(root);
}

void MainWindow::onDirSelected(EntryRef ref) {
    selectedDir_ = ref;
    graphWidget_->setDirectory(ref);
}

void MainWindow::onGraphEntrySelected(EntryRef ref) {
    const DirEntry& entry = entryStore_[ref];

    // Navigate the dir list to show this entry selected.
    dirListView_->selectEntry(ref);

    // Rebuild the graph for the relevant directory.
    EntryRef dirRef = entry.isDir() ? ref : entry.parent;
    onDirSelected(dirRef);
}

void MainWindow::showEntryContextMenu(EntryRef ref, QPoint globalPos) {
    if (!ref.valid())
        return;

    const DirEntry& entry = entryStore_[ref];
    const QString path = entryFullPath(entryStore_, nameStore_, ref);

    QMenu menu(this);

    auto* openAction = menu.addAction(
        QIcon::fromTheme("document-open-symbolic"), tr("Open"),
        QKeySequence(Qt::CTRL | Qt::Key_O));

    auto* terminalAction = menu.addAction(
        QIcon::fromTheme("utilities-terminal-symbolic"), tr("Open Terminal"),
        QKeySequence(Qt::CTRL | Qt::Key_T));

    auto* copyAction = menu.addAction(
        QIcon::fromTheme("edit-copy-symbolic"), tr("Copy to Clipboard"),
        QKeySequence(Qt::CTRL | Qt::Key_C));

    menu.addSeparator();

    auto* trashAction = menu.addAction(
        QIcon::fromTheme("user-trash-symbolic"), tr("Move to Trash"),
        QKeySequence(Qt::Key_Delete));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (chosen == terminalAction) {
        const QString dir = entry.isDir() ? path : QFileInfo(path).path();
        QString terminal = QString::fromLocal8Bit(qgetenv("TERMINAL"));
        if (terminal.isEmpty()) {
            for (const char* candidate : {"konsole", "gnome-terminal", "xfce4-terminal",
                                          "xterm"}) {
                if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
                    terminal = candidate;
                    break;
                }
            }
        }
        if (!terminal.isEmpty())
            QProcess::startDetached(terminal, {"--workdir", dir});
    } else if (chosen == copyAction) {
        QGuiApplication::clipboard()->setText(path);
    } else if (chosen == trashAction) {
        // Cannot trash the scan root.
        if (ref == currentRoot_)
            return;

        auto answer = QMessageBox::question(
            this, tr("Move to Trash"),
            tr("Move \"%1\" to trash?").arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        if (!QFile::moveToTrash(path))
            return;

        EntryRef parentDir = entry.parent;

        // If selectedDir_ is the removed entry or a descendant of it,
        // fall back to the parent of the removed entry.
        if (selectedDir_.valid()) {
            EntryRef cur = selectedDir_;
            while (cur.valid()) {
                if (cur == ref) {
                    selectedDir_ = parentDir;
                    break;
                }
                cur = entryStore_[cur].parent;
            }
        }

        entryStore_.remove(ref);
        dirListView_->refreshAfterRemoval(ref, parentDir);

        if (selectedDir_.valid())
            graphWidget_->setDirectory(selectedDir_);
    }
}

} // namespace ldirstat
