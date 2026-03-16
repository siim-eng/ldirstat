#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirlistview.h"
#include "flamegraphwidget.h"
#include "scanprogresswidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QMenuBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>

namespace ldirstat {

void MainWindowBuilder::build(MainWindow* w) {
    w->setWindowTitle("LDirStat");
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
    w->dirListView_ = new DirListView(w);
    w->flameGraphWidget_ = new FlameGraphWidget(w);

    // Scan progress widget (shown during scanning).
    w->scanProgress_ = new ScanProgressWidget(w);

    // Flame stack: progress (index 0) / flamegraph (index 1).
    w->flameStack_ = new QStackedWidget(w);
    w->flameStack_->addWidget(w->scanProgress_);
    w->flameStack_->addWidget(w->flameGraphWidget_);
    w->flameStack_->setCurrentIndex(1);

    // Main splitter: dir list (50%) / flame stack (50%).
    auto* mainSplitter = new QSplitter(Qt::Vertical, w);
    mainSplitter->addWidget(w->dirListView_);
    mainSplitter->addWidget(w->flameStack_);
    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 5);

    // Poll timer for scan progress.
    w->scanPollTimer_ = new QTimer(w);
    w->scanPollTimer_->setInterval(100);
    QObject::connect(w->scanPollTimer_, &QTimer::timeout,
                     w, &MainWindow::onScanPollTick);

    // Welcome widget + stacked view.
    w->welcomeWidget_ = new WelcomeWidget(w);
    w->viewStack_ = new QStackedWidget(w);
    w->viewStack_->addWidget(w->welcomeWidget_);  // index 0
    w->viewStack_->addWidget(mainSplitter);         // index 1
    w->setCentralWidget(w->viewStack_);

    // Signals.
    QObject::connect(w->dirListView_, &DirListView::directorySelected,
                     w, &MainWindow::onDirSelected);
    QObject::connect(w->flameGraphWidget_, &FlameGraphWidget::rectClicked,
                     w, &MainWindow::onFlameRectClicked);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::scanRequested,
                     w, &MainWindow::startScan);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::openDirectoryRequested,
                     w, &MainWindow::onOpenDirectory);
    QObject::connect(w->scanProgress_, &ScanProgressWidget::stopRequested,
                     w, &MainWindow::onStopScan);
}

} // namespace ldirstat
