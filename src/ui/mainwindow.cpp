#include "mainwindow.h"
#include "mainwindowbuilder.h"

#include "dirtreeview.h"
#include "filelistview.h"
#include "flamegraphwidget.h"
#include "welcomewidget.h"

#include <QFileDialog>
#include <QStackedWidget>
#include <QThread>

namespace ldirstat {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
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

    delete scanThread_;
    scanThread_ = nullptr;

    entryStore_.clear();
    nameStore_.clear();

    auto scanPath = path.toStdString();
    int workers = QThread::idealThreadCount();

    scanThread_ = QThread::create([this, scanPath, workers]() {
        Scanner scanner(entryStore_, nameStore_);
        EntryRef root = scanner.scan(scanPath, workers);
        scanner.propagate(root);
        emit scanComplete(root);
    });
    scanThread_->start();
}

void MainWindow::onScanFinished(EntryRef root) {
    if (scanThread_) {
        scanThread_->wait();
        delete scanThread_;
        scanThread_ = nullptr;
    }

    currentRoot_ = root;
    selectedDir_ = root;

    dirTree_->setRoot(entryStore_, nameStore_, root);
    onDirSelected(root);
}

void MainWindow::onDirSelected(EntryRef ref) {
    selectedDir_ = ref;

    fileList_->showTopFiles(entryStore_, nameStore_, ref);

    flameGraph_.build(entryStore_, ref);
    flameGraphWidget_->setGraph(&flameGraph_, &entryStore_, &nameStore_);
}

void MainWindow::onFlameRectClicked(EntryRef ref) {
    if (!entryStore_[ref].isDir())
        return;

    dirTree_->selectEntry(ref);
    onDirSelected(ref);
}

} // namespace ldirstat
