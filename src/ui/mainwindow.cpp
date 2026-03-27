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
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
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
#include <QVBoxLayout>

#include <algorithm>

namespace ldirstat {

namespace {

bool permanentlyRemovePath(const QString& path) {
    const QFileInfo info(path);
    // Check symlinks before isDir(); following a directory symlink here would be dangerous.
    if (info.isSymLink() || !info.isDir())
        return QFile::remove(path);
    return QDir(path).removeRecursively();
}

} // namespace

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

void MainWindow::openEntry(EntryRef ref) {
    if (!ref.valid() || !isGraphPageVisible())
        return;

    const QString path = pathForEntry(ref);
    if (path.isEmpty())
        return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openEntryTerminal(EntryRef ref) {
    if (!ref.valid() || !isGraphPageVisible())
        return;

    const DirEntry& entry = entryStore_[ref];
    const QString path = pathForEntry(ref);
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

void MainWindow::copyEntryPath(EntryRef ref) {
    if (!ref.valid() || !isGraphPageVisible())
        return;

    const QString path = pathForEntry(ref);
    if (path.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(path);
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
    if (deleteEntryPermanentlyAction_)
        deleteEntryPermanentlyAction_->setEnabled(enabled && currentEntry_ != currentRoot_);
}

bool MainWindow::dirListSelectionIsActive() const {
    if (!dirListView_ || !isGraphPageVisible())
        return false;
    if (contextMenuFromDirList_)
        return true;

    QWidget* focusWidget = QApplication::focusWidget();
    return focusWidget != nullptr &&
           (focusWidget == dirListView_ || dirListView_->isAncestorOf(focusWidget));
}

std::vector<EntryRef> MainWindow::actionTargets() const {
    if (dirListSelectionIsActive())
        return collapseNestedTargets(dirListView_->selectedEntries());

    std::vector<EntryRef> refs;

    if (currentEntry_.valid() && currentEntry_ != currentRoot_)
        refs.push_back(currentEntry_);

    return collapseNestedTargets(refs);
}

std::vector<EntryRef> MainWindow::collapseNestedTargets(const std::vector<EntryRef>& refs) const {
    std::vector<EntryRef> collapsed;
    collapsed.reserve(refs.size());

    for (EntryRef ref : refs) {
        if (!ref.valid() || ref == currentRoot_)
            continue;

        bool containedByExisting = false;
        for (EntryRef existing : collapsed) {
            if (isEntryInSubtree(ref, existing)) {
                containedByExisting = true;
                break;
            }
        }
        if (containedByExisting)
            continue;

        collapsed.erase(
            std::remove_if(collapsed.begin(), collapsed.end(),
                           [this, ref](EntryRef existing) {
                               return isEntryInSubtree(existing, ref);
                           }),
            collapsed.end());
        collapsed.push_back(ref);
    }

    return collapsed;
}

void MainWindow::applyPostRemovalState(const std::vector<EntryRef>& removedRefs) {
    if (!currentRoot_.valid())
        return;

    bool currentEntryRemoved = false;
    bool graphFocusRemoved = false;
    EntryRef fallbackDir = currentRoot_;

    for (EntryRef ref : removedRefs) {
        if (!ref.valid())
            continue;

        const EntryRef parentDir = entryStore_[ref].parent;
        if (parentDir.valid())
            fallbackDir = parentDir;

        currentEntryRemoved = currentEntryRemoved ||
            (currentEntry_.valid() && isEntryInSubtree(currentEntry_, ref));
        graphFocusRemoved = graphFocusRemoved ||
            (graphFocusDir_.valid() && isEntryInSubtree(graphFocusDir_, ref));
    }

    for (EntryRef ref : removedRefs) {
        if (!isEntryInSubtree(ref, currentRoot_))
            continue;
        entryStore_.remove(ref);
    }

    if (currentEntryRemoved)
        setCurrentEntry(kNoEntry);

    if (currentEntry_.valid())
        graphFocusDir_ = graphFocusForEntry(currentEntry_);
    else if (graphFocusRemoved)
        graphFocusDir_ = fallbackDir.valid() ? fallbackDir : currentRoot_;

    if (dirListView_) {
        const EntryRef target =
            currentEntry_.valid() ? currentEntry_
                                  : (graphFocusDir_.valid() ? graphFocusDir_ : currentRoot_);
        dirListView_->selectEntry(target);
    }

    syncGraphSelection();
}

void MainWindow::showBatchFailureDialog(const QString& title,
                                        const QString& actionDescription,
                                        const QStringList& failedPaths) {
    if (failedPaths.isEmpty())
        return;

    QStringList lines;
    const int limit = 8;
    const int shownCount = std::min(limit, static_cast<int>(failedPaths.size()));
    for (int i = 0; i < shownCount; ++i)
        lines.push_back(failedPaths[i]);
    if (failedPaths.size() > shownCount)
        lines.push_back(tr("...and %1 more").arg(failedPaths.size() - shownCount));

    QMessageBox::warning(
        this, title,
        tr("Failed to %1 %2 entr%3:\n%4")
            .arg(actionDescription)
            .arg(failedPaths.size())
            .arg(failedPaths.size() == 1 ? tr("y") : tr("ies"))
            .arg(lines.join('\n')));
}

void MainWindow::startContinueScan(EntryRef ref) {
    if (!ref.valid() || !currentRoot_.valid())
        return;
    if (mountProcess_ || (scanThread_ && scanThread_->isRunning()))
        return;
    if (!entryStore_[ref].isMountPoint())
        return;

    toolbar_->setVisible(true);
    viewStack_->setCurrentIndex(1);
    flameStack_->setCurrentIndex(0);
    scanProgress_->reset();
    scanPollTimer_->start();
    dirListView_->setEnabled(false);
    overviewAction_->setEnabled(false);
    rescanAction_->setEnabled(false);

    delete scanThread_;
    scanThread_ = nullptr;

    activeScanMode_ = ScanMode::ContinueMountPoint;
    pendingContinueMount_ = ref;

    const int workers = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
    scanThread_ = QThread::create([this, ref, workers]() {
        const bool success = scanner_.continueScan(ref, workers);
        emit scanComplete(success ? ref : kNoEntry);
    });
    scanThread_->start();
}

void MainWindow::showEntryContextMenuInternal(EntryRef ref, QPoint globalPos, bool fromDirList) {
    if (!ref.valid())
        return;

    contextMenuFromDirList_ = fromDirList;

    QMenu menu(this);
    if (fromDirList) {
        menu.addAction(openEntryAction_->icon(), openEntryAction_->text(),
                       this, [this, ref]() { openEntry(ref); });
        menu.addAction(openEntryTerminalAction_->icon(), openEntryTerminalAction_->text(),
                       this, [this, ref]() { openEntryTerminal(ref); });
        menu.addAction(copyEntryPathAction_->icon(), copyEntryPathAction_->text(),
                       this, [this, ref]() { copyEntryPath(ref); });
    } else {
        setCurrentEntry(ref);
        syncGraphHighlight();
        menu.addAction(openEntryAction_);
        menu.addAction(openEntryTerminalAction_);
        menu.addAction(copyEntryPathAction_);
    }
    if (entryStore_[ref].isMountPoint()) {
        menu.addAction(tr("Continue Scanning at Mount Point"),
                       this, [this, ref]() { startContinueScan(ref); });
    }
    menu.addSeparator();
    menu.addAction(trashEntryAction_);
    menu.addAction(deleteEntryPermanentlyAction_);
    menu.exec(globalPos);

    contextMenuFromDirList_ = false;
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
        if (dirListView_->handleArrowKey(keyEvent->key(), keyEvent->modifiers()))
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
    const Qt::KeyboardModifiers supportedModifiers =
        Qt::ShiftModifier | Qt::ControlModifier;
    if ((keyEvent->modifiers() & ~supportedModifiers) != Qt::NoModifier)
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
    if (qobject_cast<QLineEdit*>(widget))
        return false;
    if (widget->inherits("QTextEdit") || widget->inherits("QPlainTextEdit") ||
        widget->inherits("QAbstractSpinBox")) {
        return false;
    }

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

    activeScanMode_ = ScanMode::FullRootScan;
    pendingContinueMount_ = kNoEntry;
    lastScanPath_ = path;

    toolbar_->setVisible(true);
    overviewAction_->setText(tr("Overview"));
    rescanAction_->setVisible(true);
    overviewAction_->setEnabled(true);
    rescanAction_->setEnabled(true);

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
        if (root.valid()) {
            scanner_.propagate(root);
            scanner_.sortBySize(workers);
        }
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

    if (activeScanMode_ == ScanMode::ContinueMountPoint) {
        const EntryRef continuedMount = pendingContinueMount_;
        const EntryRef fallbackTarget = currentEntry_.valid()
            ? currentEntry_
            : (graphFocusDir_.valid() ? graphFocusDir_ : currentRoot_);
        bool continueSucceeded = false;

        if (scanner_.stopped()) {
            scanner_.revertContinueScan(continuedMount);
        } else if (!root.valid()) {
            scanner_.revertContinueScan(continuedMount);
            QMessageBox::warning(
                this,
                tr("Continue Scan Failed"),
                tr("Unable to continue scanning \"%1\".")
                    .arg(pathForEntry(continuedMount)));
        } else {
            onScanPollTick();
            scanner_.commitContinueScan(continuedMount);
            continueSucceeded = true;
        }

        flameStack_->setCurrentIndex(1);
        dirListView_->setEnabled(true);
        overviewAction_->setEnabled(true);
        rescanAction_->setEnabled(true);

        if (currentRoot_.valid()) {
            dirListView_->setRoot(entryStore_, nameStore_, currentRoot_);
            graphWidget_->setStores(&entryStore_, &nameStore_);

            const EntryRef refreshTarget =
                continueSucceeded ? continuedMount : fallbackTarget;
            const EntryRef target = refreshTarget.valid() ? refreshTarget : currentRoot_;
            if (target.valid() && target != currentRoot_)
                dirListView_->selectEntry(target);

            syncGraphSelection();
        }

        activeScanMode_ = ScanMode::FullRootScan;
        pendingContinueMount_ = kNoEntry;
        return;
    }

    if (scanner_.stopped()) {
        entryStore_.clear();
        nameStore_.clear();
        setCurrentEntry(kNoEntry);
        graphFocusDir_ = kNoEntry;
        dirListView_->setEnabled(true);
        viewStack_->setCurrentIndex(0);
        refreshWelcomeVolumes();
        activeScanMode_ = ScanMode::FullRootScan;
        pendingContinueMount_ = kNoEntry;
        if (!currentRoot_.valid()) {
            toolbar_->setVisible(false);
        } else {
            overviewAction_->setText(tr("Back"));
            rescanAction_->setVisible(false);
        }
        return;
    }

    if (!root.valid()) {
        entryStore_.clear();
        nameStore_.clear();
        setCurrentEntry(kNoEntry);
        graphFocusDir_ = kNoEntry;
        dirListView_->setEnabled(true);
        viewStack_->setCurrentIndex(0);
        refreshWelcomeVolumes();
        toolbar_->setVisible(false);
        activeScanMode_ = ScanMode::FullRootScan;
        pendingContinueMount_ = kNoEntry;
        QMessageBox::warning(
            this,
            tr("Scan Failed"),
            tr("Unable to scan \"%1\".").arg(lastScanPath_));
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
    activeScanMode_ = ScanMode::FullRootScan;
    pendingContinueMount_ = kNoEntry;
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
    openEntry(currentEntry_);
}

void MainWindow::openCurrentEntryTerminal() {
    openEntryTerminal(currentEntry_);
}

void MainWindow::copyCurrentEntryPath() {
    copyEntryPath(currentEntry_);
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

void MainWindow::openHelpPage() {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://github.com/siim-eng/ldirstat/blob/main/docs/HELP.md")));
}

void MainWindow::reportIssue() {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://github.com/siim-eng/ldirstat/issues")));
}

void MainWindow::showAboutDialog() {
    QDialog dialog(this);
    const QString appName = QApplication::applicationName();
    const QString appVersion = QApplication::applicationVersion();

    dialog.setWindowTitle(tr("About %1").arg(appName));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QLabel(
        tr("<p><b>%1</b><br>Version %2</p>"
           "<p>LDirStat scans directories and helps you understand disk usage with flame graph and tree map views. "
           "It is built for fast, low-latency analysis with low memory usage for exploring large directory trees.</p>"
           "<p>Copyright Siim Suisalu 2026<br>"
           "License: MIT</p>"
           "<p><a href=\"https://github.com/siim-eng/ldirstat\">Source code repository</a></p>")
            .arg(appName, appVersion),
        &dialog);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->setOpenExternalLinks(true);
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::trashCurrentEntry() {
    if (!isGraphPageVisible())
        return;
    if (!contextMenuFromDirList_) {
        QWidget* focusWidget = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(focusWidget))
            return;
    }

    const std::vector<EntryRef> refs = actionTargets();
    if (refs.empty())
        return;

    const QString prompt = refs.size() == 1
        ? tr("Move \"%1\" to trash?").arg(pathForEntry(refs.front()))
        : tr("Move %1 selected entries to trash?").arg(refs.size());
    const auto answer = QMessageBox::question(
        this, tr("Move to Trash"), prompt,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    std::vector<EntryRef> removedRefs;
    removedRefs.reserve(refs.size());
    QStringList failedPaths;
    for (EntryRef ref : refs) {
        const QString path = pathForEntry(ref);
        if (path.isEmpty())
            continue;
        if (!QFile::moveToTrash(path)) {
            failedPaths.push_back(path);
            continue;
        }
        removedRefs.push_back(ref);
    }

    if (removedRefs.empty() && failedPaths.isEmpty())
        return;

    if (!removedRefs.empty())
        applyPostRemovalState(removedRefs);
    showBatchFailureDialog(tr("Move to Trash"), tr("move to trash"), failedPaths);
}

void MainWindow::deleteCurrentEntryPermanently() {
    if (!isGraphPageVisible())
        return;
    if (!contextMenuFromDirList_) {
        QWidget* focusWidget = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(focusWidget))
            return;
    }

    const std::vector<EntryRef> refs = actionTargets();
    if (refs.empty())
        return;

    QString prompt;
    if (refs.size() == 1) {
        prompt = tr("Type yes to permanently delete \"%1\".").arg(pathForEntry(refs.front()));
    } else {
        prompt = tr("Type yes to permanently delete %1 selected entries.").arg(refs.size());
    }

    bool ok = false;
    const QString confirmation = QInputDialog::getText(
        this, tr("Delete Permanently"), prompt, QLineEdit::Normal, {}, &ok);
    if (!ok || confirmation != QStringLiteral("yes"))
        return;

    std::vector<EntryRef> removedRefs;
    removedRefs.reserve(refs.size());
    QStringList failedPaths;
    for (EntryRef ref : refs) {
        const QString path = pathForEntry(ref);
        if (path.isEmpty())
            continue;
        if (!permanentlyRemovePath(path)) {
            failedPaths.push_back(path);
            continue;
        }
        removedRefs.push_back(ref);
    }

    if (removedRefs.empty() && failedPaths.isEmpty())
        return;

    if (!removedRefs.empty())
        applyPostRemovalState(removedRefs);
    showBatchFailureDialog(tr("Delete Permanently"), tr("delete permanently"), failedPaths);
}

void MainWindow::showEntryContextMenu(EntryRef ref, QPoint globalPos) {
    showEntryContextMenuInternal(ref, globalPos, false);
}

void MainWindow::showDirListContextMenu(EntryRef ref, QPoint globalPos) {
    showEntryContextMenuInternal(ref, globalPos, true);
}

} // namespace ldirstat
