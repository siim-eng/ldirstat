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
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
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
    updateBreadcrumbPath();

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

bool MainWindow::isGraphPageVisible() const {
    return viewStack_ && flameStack_ && graphTypeStack_ &&
           viewStack_->currentWidget() != welcomeWidget_ &&
           flameStack_->currentWidget() == graphTypeStack_;
}

EntryRef MainWindow::graphFocusForEntry(EntryRef ref) const {
    if (!currentRoot_.valid())
        return kNoEntry;
    if (!ref.valid())
        return currentRoot_;

    const EntryRef parent = entryStore_[ref].parent;
    return parent.valid() ? parent : ref;
}

bool MainWindow::isEntryInSubtree(EntryRef ref, EntryRef ancestor) const {
    while (ref.valid()) {
        if (ref == ancestor)
            return true;
        ref = entryStore_[ref].parent;
    }
    return false;
}

void MainWindow::setCurrentEntry(EntryRef ref) {
    currentEntry_ = ref;
    updateEntryActions();
}

void MainWindow::syncGraphHighlight() {
    if (!graphWidget_)
        return;

    graphWidget_->setSelectedEntry(currentEntry_);
}

void MainWindow::syncGraphSelection() {
    if (!graphWidget_)
        return;

    const EntryRef focusDir = graphFocusDir_.valid() ? graphFocusDir_ : currentRoot_;
    graphWidget_->setDirectory(focusDir.valid() ? focusDir : kNoEntry);
    syncGraphHighlight();
    updateBreadcrumbPath();
}

void MainWindow::updateEntryActions() {
    const bool enabled = isGraphPageVisible() && currentEntry_.valid();
    if (openEntryAction_)
        openEntryAction_->setEnabled(enabled);
    if (openEntryTerminalAction_)
        openEntryTerminalAction_->setEnabled(enabled);
    if (copyEntryPathAction_)
        copyEntryPathAction_->setEnabled(enabled);
    if (trashEntryAction_)
        trashEntryAction_->setEnabled(enabled && currentEntry_ != currentRoot_);
}

QString MainWindow::pathForEntry(EntryRef ref) const {
    if (!ref.valid())
        return {};
    return entryFullPath(entryStore_, nameStore_, ref);
}

EntryRef MainWindow::breadcrumbDirectory() const {
    if (currentEntry_.valid()) {
        const DirEntry& entry = entryStore_[currentEntry_];
        if (entry.isDir())
            return currentEntry_;
    }

    if (graphFocusDir_.valid())
        return graphFocusDir_;

    return currentRoot_;
}

void MainWindow::navigateToDirectory(EntryRef ref) {
    if (!ref.valid())
        return;

    setCurrentEntry(ref);
    graphFocusDir_ = graphFocusForEntry(ref);
    dirListView_->selectEntry(ref);
    syncGraphSelection();
}

void MainWindow::updateBreadcrumbPath() {
    if (!breadcrumbPathWidget_ || !breadcrumbPathLayout_ ||
        !breadcrumbCopyButton_ || !breadcrumbClearButton_)
        return;

    while (QLayoutItem* item = breadcrumbPathLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const EntryRef focusDir = breadcrumbDirectory();
    const QString path = pathForEntry(focusDir);
    const QString rootPath = pathForEntry(currentRoot_);

    breadcrumbPathWidget_->setToolTip(path);
    breadcrumbCopyButton_->setEnabled(!path.isEmpty());
    breadcrumbClearButton_->setEnabled(currentRoot_.valid() && focusDir.valid() &&
                                       focusDir != currentRoot_);

    if (path.isEmpty()) {
        auto* placeholder = new QLabel(tr("Directory path"), breadcrumbPathWidget_);
        placeholder->setEnabled(false);
        breadcrumbPathLayout_->addWidget(placeholder);
        breadcrumbPathLayout_->addStretch();
        return;
    }

    std::vector<EntryRef> dirChain;
    EntryRef cur = focusDir;
    while (cur.valid()) {
        dirChain.push_back(cur);
        cur = entryStore_[cur].parent;
    }
    std::reverse(dirChain.begin(), dirChain.end());

    auto splitPath = [](const QString& value) {
        const QString normalized = QDir::cleanPath(value);
        QStringList parts = normalized.split(QDir::separator(), Qt::SkipEmptyParts);
        if (normalized.startsWith(QDir::separator()))
            parts.prepend(QString(1, QDir::separator()));
        if (parts.isEmpty())
            parts.append(QString(1, QDir::separator()));
        return parts;
    };

    const QStringList pathParts = splitPath(path);
    const QStringList rootParts = rootPath.isEmpty() ? QStringList{} : splitPath(rootPath);
    const int rootStartIndex = rootParts.isEmpty() ? -1 : static_cast<int>(rootParts.size()) - 1;

    for (int i = 0; i < pathParts.size(); ++i) {
        if (i > 0) {
            auto* separator = new QLabel(QStringLiteral(">"), breadcrumbPathWidget_);
            separator->setEnabled(false);
            breadcrumbPathLayout_->addWidget(separator);
        }

        auto* button = new QToolButton(breadcrumbPathWidget_);
        button->setText(pathParts[i]);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::NoFocus);

        const int chainIndex = i - rootStartIndex;
        const bool isClickable = chainIndex > 0 && chainIndex < static_cast<int>(dirChain.size());
        if (isClickable) {
            const EntryRef targetDir = dirChain[chainIndex];
            connect(button, &QToolButton::clicked, this, [this, targetDir]() {
                navigateToDirectory(targetDir);
            });
        } else {
            button->setEnabled(false);
        }

        breadcrumbPathLayout_->addWidget(button);
    }

    breadcrumbPathLayout_->addStretch();
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

    if (!isGraphPageVisible())
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
        if (currentRoot_.valid())
            graphFocusDir_ = currentRoot_;
        setCurrentEntry(kNoEntry);
        syncGraphSelection();
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
    currentEntry_ = kNoEntry;
    graphFocusDir_ = kNoEntry;
    updateEntryActions();
    updateBreadcrumbPath();

    entryStore_.clear();
    nameStore_.clear();
    if (graphTypeStack_) {
        for (int i = 0; i < graphTypeStack_->count(); ++i) {
            auto* widget = qobject_cast<GraphWidget*>(graphTypeStack_->widget(i));
            if (!widget)
                continue;
            widget->setStores(nullptr, nullptr);
            widget->setDirectory(kNoEntry);
            widget->setSelectedEntry(kNoEntry);
        }
    }

    auto scanPath = path.toStdString();
    int workers = std::clamp(QThread::idealThreadCount() / 2, 1, 8);

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

    setCurrentEntry(kNoEntry);
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
        setCurrentEntry(kNoEntry);
        graphFocusDir_ = kNoEntry;
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
    setCurrentEntry(kNoEntry);
    graphFocusDir_ = root;

    dirListView_->setRoot(entryStore_, nameStore_, root);
    graphWidget_->setStores(&entryStore_, &nameStore_);
    syncGraphSelection();
}

void MainWindow::onDirSelected(EntryRef ref) {
    graphFocusDir_ = ref.valid() ? ref : currentRoot_;
    syncGraphSelection();
}

void MainWindow::onEntrySelected(EntryRef ref) {
    setCurrentEntry(ref);
}

void MainWindow::onGraphEntrySelected(EntryRef ref) {
    setCurrentEntry(ref);
    graphFocusDir_ = graphFocusForEntry(ref);

    // Navigate the dir list to show this entry selected.
    dirListView_->selectEntry(ref);
    syncGraphSelection();
}

void MainWindow::openCurrentEntry() {
    if (!currentEntry_.valid() || !isGraphPageVisible())
        return;

    const QString path = pathForEntry(currentEntry_);
    if (path.isEmpty())
        return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openCurrentEntryTerminal() {
    if (!currentEntry_.valid() || !isGraphPageVisible())
        return;

    const DirEntry& entry = entryStore_[currentEntry_];
    const QString path = pathForEntry(currentEntry_);
    if (path.isEmpty())
        return;

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
}

void MainWindow::copyCurrentEntryPath() {
    if (!currentEntry_.valid() || !isGraphPageVisible())
        return;

    const QString path = pathForEntry(currentEntry_);
    if (path.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(path);
}

void MainWindow::copyCurrentDirectoryPath() {
    const EntryRef focusDir = breadcrumbDirectory();
    if (!focusDir.valid())
        return;

    const QString path = pathForEntry(focusDir);
    if (path.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(path);
}

void MainWindow::clearDirectoryBreadcrumb() {
    if (!currentRoot_.valid() || breadcrumbDirectory() == currentRoot_)
        return;

    setCurrentEntry(kNoEntry);
    graphFocusDir_ = currentRoot_;
    dirListView_->selectEntry(currentRoot_);
    syncGraphSelection();
}

void MainWindow::trashCurrentEntry() {
    if (!currentEntry_.valid() || !isGraphPageVisible() || currentEntry_ == currentRoot_)
        return;

    const EntryRef ref = currentEntry_;
    const DirEntry& entry = entryStore_[ref];
    const QString path = pathForEntry(ref);
    if (path.isEmpty())
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
    const bool currentEntryRemoved =
        currentEntry_.valid() && isEntryInSubtree(currentEntry_, ref);
    const bool graphFocusRemoved =
        graphFocusDir_.valid() && isEntryInSubtree(graphFocusDir_, ref);

    entryStore_.remove(ref);
    dirListView_->refreshAfterRemoval(ref, parentDir);

    if (currentEntryRemoved)
        setCurrentEntry(kNoEntry);

    if (currentEntry_.valid()) {
        graphFocusDir_ = graphFocusForEntry(currentEntry_);
    } else if (graphFocusRemoved) {
        graphFocusDir_ = parentDir.valid() ? parentDir : currentRoot_;
    }

    syncGraphSelection();
}

void MainWindow::showEntryContextMenu(EntryRef ref, QPoint globalPos) {
    if (!ref.valid())
        return;

    setCurrentEntry(ref);
    syncGraphHighlight();

    QMenu menu(this);
    menu.addAction(openEntryAction_);
    menu.addAction(openEntryTerminalAction_);
    menu.addAction(copyEntryPathAction_);
    menu.addSeparator();
    menu.addAction(trashEntryAction_);
    menu.exec(globalPos);
}

} // namespace ldirstat
