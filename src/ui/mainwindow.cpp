#include "mainwindow.h"
#include "mainwindowbuilder.h"

#include "dirlistview.h"
#include "flamegraphwidget.h"
#include "scanprogresswidget.h"
#include "welcomewidget.h"

#include <QFileDialog>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>

namespace ldirstat {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , scanner_(entryStore_, nameStore_) {
    MainWindowBuilder::build(this);

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

void MainWindow::onOpenDirectory() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Directory"), QDir::homePath());
    if (!dir.isEmpty())
        startScan(dir);
}

void MainWindow::startScan(const QString& path) {
    if (scanThread_ && scanThread_->isRunning())
        return;

    viewStack_->setCurrentIndex(1);
    flameStack_->setCurrentIndex(0);
    scanProgress_->reset();
    scanPollTimer_->start();

    delete scanThread_;
    scanThread_ = nullptr;

    entryStore_.clear();
    nameStore_.clear();

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
        viewStack_->setCurrentIndex(0);
        return;
    }

    onScanPollTick();
    flameStack_->setCurrentIndex(1);

    currentRoot_ = root;
    selectedDir_ = root;

    dirListView_->setRoot(entryStore_, nameStore_, root);
    onDirSelected(root);
}

void MainWindow::onDirSelected(EntryRef ref) {
    selectedDir_ = ref;

    flameGraph_.build(entryStore_, ref);
    flameGraphWidget_->setGraph(&flameGraph_, &entryStore_, &nameStore_);
}

void MainWindow::onFlameRectClicked(EntryRef ref) {
    if (!entryStore_[ref].isDir())
        return;

    dirListView_->selectEntry(ref);
    onDirSelected(ref);
}

} // namespace ldirstat
