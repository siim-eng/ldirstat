#include "mainwindow.h"
#include "mainwindowbuilder.h"

#include "dirlistview.h"
#include "graphwidget.h"
#include "scanprogresswidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QEvent>
#include <QFileDialog>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>

namespace ldirstat {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , scanner_(entryStore_, nameStore_) {
    MainWindowBuilder::build(this);

    themeColors_ = ThemeColors::fromPalette(palette());
    dirListView_->setThemeColors(themeColors_);
    if (graphTypeStack_) {
        for (int i = 0; i < graphTypeStack_->count(); ++i) {
            auto* widget = qobject_cast<GraphWidget*>(graphTypeStack_->widget(i));
            if (widget)
                widget->setThemeColors(themeColors_);
        }
    }

    fileSystems_.readMounts();
    welcomeWidget_->populate(fileSystems_);

    connect(this, &MainWindow::scanComplete,
            this, &MainWindow::onScanFinished, Qt::QueuedConnection);
}

MainWindow::~MainWindow() {
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

void MainWindow::onOverview() {
    if (viewStack_->currentIndex() == 1) {
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
    if (scanThread_ && scanThread_->isRunning())
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


} // namespace ldirstat
