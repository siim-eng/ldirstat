#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirtreeview.h"
#include "filelistview.h"
#include "flamegraphwidget.h"

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
    w->dirTree_ = new DirTreeView(w);
    w->fileList_ = new FileListView(w);
    w->flameGraphWidget_ = new FlameGraphWidget(w);

    // Right splitter: file list (top 30%) / flamegraph (bottom 70%).
    auto* rightSplitter = new QSplitter(Qt::Vertical, w);
    rightSplitter->addWidget(w->fileList_);
    rightSplitter->addWidget(w->flameGraphWidget_);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 7);

    // Main splitter: dir tree (left 15%) / right panel (85%).
    auto* mainSplitter = new QSplitter(Qt::Horizontal, w);
    mainSplitter->addWidget(w->dirTree_);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 15);
    mainSplitter->setStretchFactor(1, 85);

    w->setCentralWidget(mainSplitter);

    // Signals.
    QObject::connect(w->dirTree_, &DirTreeView::directorySelected,
                     w, &MainWindow::onDirSelected);
    QObject::connect(w->flameGraphWidget_, &FlameGraphWidget::rectClicked,
                     w, &MainWindow::onFlameRectClicked);
}

} // namespace ldirstat
