#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirlistview.h"
#include "flamegraphwidget.h"
#include "scanprogresswidget.h"
#include "treemapwidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace ldirstat {

void MainWindowBuilder::build(MainWindow* w) {
    configureWindow(w);
    buildToolbar(w);
    buildAnalysisWidgets(w);
    buildScanPollTimer(w);
    buildCentralView(w);
    const GraphTypeActions graphTypeActions = buildGraphTypeMenu(w);
    connectCoreSignals(w);
    connectGraphTypeSignals(w, graphTypeActions);
    connectVisibilitySignals(w);
    activateGraphWidget(w, w->flameGraphWidget_, MainWindow::tr("Flame Graph"));
    updateGraphTypeButtonVisibility(w);
}

void MainWindowBuilder::configureWindow(MainWindow* w) {
    w->setWindowTitle("LDirStat");
    w->resize(1200, 800);
}

void MainWindowBuilder::buildToolbar(MainWindow* w) {
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

    w->graphTypeButton_ = new QToolButton(w->toolbar_);
    w->graphTypeButton_->setText(MainWindow::tr("Graph Type"));
    w->graphTypeButton_->setPopupMode(QToolButton::InstantPopup);
    w->graphTypeButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
}

void MainWindowBuilder::buildAnalysisWidgets(MainWindow* w) {
    w->dirListView_ = new DirListView(w);
    w->flameGraphWidget_ = new FlameGraphWidget(w);
    w->treeMapWidget_ = new TreeMapWidget(w);
    w->treeMapWidget_->setRenderMode(TreeMapWidget::RenderMode::DirectoryHeaders);
    w->graphWidget_ = w->flameGraphWidget_;

    w->scanProgress_ = new ScanProgressWidget(w);

    w->graphTypeStack_ = new QStackedWidget(w);
    w->graphTypeStack_->addWidget(w->flameGraphWidget_);
    w->graphTypeStack_->addWidget(w->treeMapWidget_);
    w->graphTypeStack_->setCurrentWidget(w->flameGraphWidget_);

    w->flameStack_ = new QStackedWidget(w);
    w->flameStack_->addWidget(w->scanProgress_);
    w->flameStack_->addWidget(w->graphTypeStack_);
    w->flameStack_->setCurrentIndex(1);
}

void MainWindowBuilder::buildScanPollTimer(MainWindow* w) {
    w->scanPollTimer_ = new QTimer(w);
    w->scanPollTimer_->setInterval(100);
    QObject::connect(w->scanPollTimer_, &QTimer::timeout,
                     w, &MainWindow::onScanPollTick);
}

void MainWindowBuilder::buildCentralView(MainWindow* w) {
    auto* mainSplitter = new QSplitter(Qt::Vertical, w);
    mainSplitter->addWidget(w->dirListView_);
    mainSplitter->addWidget(w->flameStack_);
    int totalHeight = w->height();
    mainSplitter->setSizes({totalHeight * 60 / 100, totalHeight * 40 / 100});

    w->welcomeWidget_ = new WelcomeWidget(w);
    w->viewStack_ = new QStackedWidget(w);
    w->viewStack_->addWidget(w->welcomeWidget_);
    w->viewStack_->addWidget(mainSplitter);
    w->setCentralWidget(w->viewStack_);
}

MainWindowBuilder::GraphTypeActions MainWindowBuilder::buildGraphTypeMenu(MainWindow* w) {
    auto* graphTypeMenu = new QMenu(w->graphTypeButton_);
    auto* graphTypeGroup = new QActionGroup(graphTypeMenu);
    graphTypeGroup->setExclusive(true);

    GraphTypeActions actions;
    actions.flameGraph = graphTypeMenu->addAction(MainWindow::tr("Flame Graph"));
    actions.flameGraph->setCheckable(true);
    actions.flameGraph->setChecked(true);
    graphTypeGroup->addAction(actions.flameGraph);

    actions.treeMapHeaders =
        graphTypeMenu->addAction(MainWindow::tr("Tree Map - Directory Headers"));
    actions.treeMapHeaders->setCheckable(true);
    graphTypeGroup->addAction(actions.treeMapHeaders);

    actions.treeMapPacked =
        graphTypeMenu->addAction(MainWindow::tr("Tree Map no headers"));
    actions.treeMapPacked->setCheckable(true);
    graphTypeGroup->addAction(actions.treeMapPacked);

    w->graphTypeButton_->setMenu(graphTypeMenu);
    w->toolbar_->addWidget(w->graphTypeButton_);
    w->graphTypeButton_->setVisible(false);

    return actions;
}

void MainWindowBuilder::connectCoreSignals(MainWindow* w) {
    QObject::connect(w->dirListView_, &DirListView::directorySelected,
                     w, &MainWindow::onDirSelected);
    QObject::connect(w->flameGraphWidget_, &GraphWidget::entrySelected,
                     w, &MainWindow::onGraphEntrySelected);
    QObject::connect(w->treeMapWidget_, &GraphWidget::entrySelected,
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

void MainWindowBuilder::connectGraphTypeSignals(MainWindow* w, const GraphTypeActions& actions) {
    QObject::connect(actions.flameGraph, &QAction::triggered, w, [w]() {
        activateGraphWidget(w, w->flameGraphWidget_, w->tr("Flame Graph"));
    });
    QObject::connect(actions.treeMapHeaders, &QAction::triggered, w, [w]() {
        w->treeMapWidget_->setRenderMode(TreeMapWidget::RenderMode::DirectoryHeaders);
        activateGraphWidget(w, w->treeMapWidget_, w->tr("Tree Map - Directory Headers"));
    });
    QObject::connect(actions.treeMapPacked, &QAction::triggered, w, [w]() {
        w->treeMapWidget_->setRenderMode(TreeMapWidget::RenderMode::Packed);
        activateGraphWidget(w, w->treeMapWidget_, w->tr("Tree Map no headers"));
    });
}

void MainWindowBuilder::connectVisibilitySignals(MainWindow* w) {
    QObject::connect(w->viewStack_, &QStackedWidget::currentChanged,
                     w, [w](int) {
        updateGraphTypeButtonVisibility(w);
    });
    QObject::connect(w->flameStack_, &QStackedWidget::currentChanged,
                     w, [w](int) {
        updateGraphTypeButtonVisibility(w);
    });
}

void MainWindowBuilder::updateGraphTypeButtonVisibility(MainWindow* w) {
    if (!w->graphTypeButton_)
        return;

    w->graphTypeButton_->setVisible(
        w->viewStack_ && w->flameStack_ &&
        w->viewStack_->currentIndex() == 1 &&
        w->flameStack_->currentIndex() == 1);
}

void MainWindowBuilder::activateGraphWidget(MainWindow* w,
                                            GraphWidget* widget,
                                            const QString& label) {
    w->graphWidget_ = widget;
    w->graphTypeStack_->setCurrentWidget(widget);
    w->graphTypeButton_->setText(label);
    w->graphWidget_->setThemeColors(w->themeColors_);

    if (w->currentRoot_.valid())
        w->graphWidget_->setStores(&w->entryStore_, &w->nameStore_);
    else
        w->graphWidget_->setStores(nullptr, nullptr);

    if (w->selectedDir_.valid())
        w->graphWidget_->setDirectory(w->selectedDir_);
    else
        w->graphWidget_->setDirectory(kNoEntry);
}

} // namespace ldirstat
