#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirlistview.h"
#include "flamegraphwidget.h"
#include "scanprogresswidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QFileDialog>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>

namespace ldirstat {

void MainWindowBuilder::build(MainWindow* w) {
    w->setWindowTitle("LDirStat");
    w->resize(1200, 800);

    // Toolbar (hidden until first scan).
    w->toolbar_ = w->addToolBar(MainWindow::tr("Main"));
    w->toolbar_->setMovable(false);
    w->toolbar_->setFloatable(false);
    w->toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w->toolbar_->setVisible(false);

    w->overviewAction_ = w->toolbar_->addAction(
        QIcon::fromTheme("go-home-symbolic"), MainWindow::tr("Overview"));
    QObject::connect(w->overviewAction_, &QAction::triggered, w, &MainWindow::onOverview);

    w->rescanAction_ = w->toolbar_->addAction(
        QIcon::fromTheme("view-refresh-symbolic"), MainWindow::tr("Rescan"));
    QObject::connect(w->rescanAction_, &QAction::triggered, w, &MainWindow::onRescan);

    // Widgets.
    w->dirListView_ = new DirListView(w);
    w->graphWidget_ = new FlameGraphWidget(w);

    // Scan progress widget (shown during scanning).
    w->scanProgress_ = new ScanProgressWidget(w);

    // Flame stack: progress (index 0) / flamegraph (index 1).
    w->flameStack_ = new QStackedWidget(w);
    w->flameStack_->addWidget(w->scanProgress_);
    w->flameStack_->addWidget(w->graphWidget_);
    w->flameStack_->setCurrentIndex(1);

    // Main splitter: dir list (60%) / flame stack (40%).
    auto* mainSplitter = new QSplitter(Qt::Vertical, w);
    mainSplitter->addWidget(w->dirListView_);
    mainSplitter->addWidget(w->flameStack_);
    int totalHeight = w->height();
    mainSplitter->setSizes({totalHeight * 60 / 100, totalHeight * 40 / 100});

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
    QObject::connect(w->graphWidget_, &GraphWidget::entrySelected,
                     w, &MainWindow::onGraphEntrySelected);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::scanRequested,
                     w, &MainWindow::startScan);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::openDirectoryRequested,
                     w, [w]() {
        QString dir = QFileDialog::getExistingDirectory(
            w, MainWindow::tr("Select Directory"), QDir::homePath());
        if (!dir.isEmpty())
            w->startScan(dir);
    });
    QObject::connect(w->scanProgress_, &ScanProgressWidget::stopRequested,
                     w, &MainWindow::onStopScan);
}

} // namespace ldirstat
