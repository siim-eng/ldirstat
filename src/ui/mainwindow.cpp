#include "mainwindow.h"
#include "mainwindowbuilder.h"

#include "dirtreeview.h"
#include "filelistview.h"
#include "flamegraphwidget.h"

#include <QFileDialog>
#include <QThread>

namespace ldirstat {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    MainWindowBuilder::build(this);
}

void MainWindow::onOpenDirectory() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Directory"), QDir::homePath());
    if (!dir.isEmpty())
        startScan(dir);
}

void MainWindow::startScan(const QString& path) {
    // TODO: reset stores for repeated scans (stores have mutex, not assignable).
    Scanner scanner(entryStore_, nameStore_);
    EntryRef root = scanner.scan(path.toStdString(), QThread::idealThreadCount());
    scanner.propagate(root);

    onScanFinished(root);
}

void MainWindow::onScanFinished(EntryRef root) {
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
