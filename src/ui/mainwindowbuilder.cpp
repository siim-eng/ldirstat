#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirtreeview.h"
#include "filelistview.h"
#include "flamegraphwidget.h"
#include "mountlistwidget.h"

#include <QAction>
#include <QMenuBar>
#include <QSplitter>

namespace ldirstat {

void MainWindowBuilder::build(MainWindow* w) {
    w->setWindowTitle("ldirstat");
    w->resize(1200, 800);

    // Menu bar.
    auto* fileMenu = w->menuBar()->addMenu(MainWindow::tr("&File"));
    auto* openAction = fileMenu->addAction(MainWindow::tr("&Open Directory..."));
    openAction->setShortcut(QKeySequence::Open);
    QObject::connect(openAction, &QAction::triggered, w, &MainWindow::onOpenDirectory);

    auto* quitAction = fileMenu->addAction(MainWindow::tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    QObject::connect(quitAction, &QAction::triggered, w, &QWidget::close);

    // Widgets.
    w->mountList_ = new MountListWidget(w);
    w->dirTree_ = new DirTreeView(w);
    w->fileList_ = new FileListView(w);
    w->flameGraphWidget_ = new FlameGraphWidget(w);

    int totalWidth = w->width();
    QList<int> hSizes = {totalWidth * 30 / 100, totalWidth * 70 / 100};

    // Top splitter: dir tree (left 30%) / file list (right 70%).
    auto* topSplitter = new QSplitter(Qt::Horizontal, w);
    topSplitter->addWidget(w->dirTree_);
    topSplitter->addWidget(w->fileList_);
    topSplitter->setSizes(hSizes);

    // Bottom splitter: mount list (left 30%) / flamegraph (right 70%).
    auto* bottomSplitter = new QSplitter(Qt::Horizontal, w);
    bottomSplitter->addWidget(w->mountList_);
    bottomSplitter->addWidget(w->flameGraphWidget_);
    bottomSplitter->setSizes(hSizes);

    // Main splitter: top panel (50%) / bottom panel (50%).
    auto* mainSplitter = new QSplitter(Qt::Vertical, w);
    mainSplitter->addWidget(topSplitter);
    mainSplitter->addWidget(bottomSplitter);
    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 5);

    // Sync top/bottom splitter positions.
    auto syncSplitters = [](QSplitter* src, QSplitter* dst) {
        QObject::connect(src, &QSplitter::splitterMoved, dst, [src, dst]() {
            dst->setSizes(src->sizes());
        });
    };
    syncSplitters(topSplitter, bottomSplitter);
    syncSplitters(bottomSplitter, topSplitter);

    w->setCentralWidget(mainSplitter);

    // Signals.
    QObject::connect(w->mountList_, &MountListWidget::scanRequested,
                     w, &MainWindow::startScan);
    QObject::connect(w->dirTree_, &DirTreeView::directorySelected,
                     w, &MainWindow::onDirSelected);
    QObject::connect(w->flameGraphWidget_, &FlameGraphWidget::rectClicked,
                     w, &MainWindow::onFlameRectClicked);
}

} // namespace ldirstat
