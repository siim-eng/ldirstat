#include "mainwindowbuilder.h"
#include "mainwindow.h"

#include "dirlistview.h"
#include "flamegraphwidget.h"
#include "iconutil.h"
#include "scanprogresswidget.h"
#include "treemapwidget.h"
#include "welcomewidget.h"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

namespace ldirstat {

void MainWindowBuilder::build(MainWindow *w) {
    configureWindow(w);
    buildToolbar(w);
    buildAnalysisWidgets(w);
    buildScanPollTimer(w);
    buildCentralView(w);
    const GraphTypeActions graphTypeActions = buildGraphTypeMenu(w);
    buildHelpMenu(w);
    connectCoreSignals(w);
    connectGraphTypeSignals(w, graphTypeActions);
    connectVisibilitySignals(w);
    activateGraphWidget(w, w->flameGraphWidget_, MainWindow::tr("Flame Graph"));
    updateGraphTypeButtonVisibility(w);
}

void MainWindowBuilder::configureWindow(MainWindow *w) {
    w->setWindowTitle("LDirStat");
    w->resize(1200, 800);

    auto *closeAction = new QAction(MainWindow::tr("Close"), w);
    closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    closeAction->setShortcutContext(Qt::ApplicationShortcut);
    w->addAction(closeAction);
    QObject::connect(closeAction, &QAction::triggered, w, &QWidget::close);
}

void MainWindowBuilder::buildToolbar(MainWindow *w) {
    w->toolbar_ = w->addToolBar(MainWindow::tr("Main"));
    w->toolbar_->setMovable(false);
    w->toolbar_->setFloatable(false);
    w->toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w->toolbar_->setVisible(false);

    auto *breadcrumbWidget = new QWidget(w->toolbar_);
    breadcrumbWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *breadcrumbLayout = new QHBoxLayout(breadcrumbWidget);
    breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    breadcrumbLayout->setSpacing(4);

    w->breadcrumbPathWidget_ = new QWidget(breadcrumbWidget);
    w->breadcrumbPathWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    w->breadcrumbPathLayout_ = new QHBoxLayout(w->breadcrumbPathWidget_);
    w->breadcrumbPathLayout_->setContentsMargins(0, 0, 0, 0);
    w->breadcrumbPathLayout_->setSpacing(2);

    w->breadcrumbCopyButton_ = new QToolButton(breadcrumbWidget);
    w->breadcrumbCopyButton_->setIcon(themedIcon(w, QStringLiteral("edit-copy-symbolic"), QStyle::SP_FileIcon));
    w->breadcrumbCopyButton_->setToolTip(MainWindow::tr("Copy directory path"));
    w->breadcrumbCopyButton_->setFocusPolicy(Qt::NoFocus);
    QObject::connect(w->breadcrumbCopyButton_, &QToolButton::clicked, w, &MainWindow::copyCurrentDirectoryPath);

    w->breadcrumbClearButton_ = new QToolButton(breadcrumbWidget);
    w->breadcrumbClearButton_->setIcon(
        themedIcon(w, QStringLiteral("edit-clear-symbolic"), QStyle::SP_DialogResetButton));
    w->breadcrumbClearButton_->setToolTip(MainWindow::tr("Clear to root directory"));
    w->breadcrumbClearButton_->setFocusPolicy(Qt::NoFocus);
    QObject::connect(w->breadcrumbClearButton_, &QToolButton::clicked, w, &MainWindow::clearDirectoryBreadcrumb);

    breadcrumbLayout->addWidget(w->breadcrumbPathWidget_);
    breadcrumbLayout->addWidget(w->breadcrumbCopyButton_);
    breadcrumbLayout->addWidget(w->breadcrumbClearButton_);
    w->toolbar_->addWidget(breadcrumbWidget);

    w->overviewAction_ =
        w->toolbar_->addAction(themedIcon(w, QStringLiteral("go-home-symbolic"), QStyle::SP_DirHomeIcon),
                               MainWindow::tr("Overview"));
    QObject::connect(w->overviewAction_, &QAction::triggered, w, &MainWindow::onOverview);

    w->rescanAction_ =
        w->toolbar_->addAction(themedIcon(w, QStringLiteral("view-refresh-symbolic"), QStyle::SP_BrowserReload),
                               MainWindow::tr("Rescan"));
    QObject::connect(w->rescanAction_, &QAction::triggered, w, &MainWindow::onRescan);

    auto configureEntryAction = [w](QAction *&action,
                                    const QString &iconName,
                                    QStyle::StandardPixmap fallback,
                                    const QString &text,
                                    const QKeySequence &shortcut,
                                    auto slot) {
        action = new QAction(themedIcon(w, iconName, fallback), text, w);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setEnabled(false);
        w->addAction(action);
        QObject::connect(action, &QAction::triggered, w, slot);
    };

    configureEntryAction(w->openEntryAction_,
                         QStringLiteral("document-open-symbolic"),
                         QStyle::SP_DialogOpenButton,
                         MainWindow::tr("Open"),
                         QKeySequence(Qt::CTRL | Qt::Key_O),
                         &MainWindow::openCurrentEntry);
    configureEntryAction(w->openEntryTerminalAction_,
                         QStringLiteral("utilities-terminal-symbolic"),
                         QStyle::SP_ComputerIcon,
                         MainWindow::tr("Open Terminal"),
                         QKeySequence(Qt::CTRL | Qt::Key_T),
                         &MainWindow::openCurrentEntryTerminal);
    configureEntryAction(w->copyEntryPathAction_,
                         QStringLiteral("edit-copy-symbolic"),
                         QStyle::SP_FileIcon,
                         MainWindow::tr("Copy to Clipboard"),
                         QKeySequence(Qt::CTRL | Qt::Key_C),
                         &MainWindow::copyCurrentEntryPath);
    configureEntryAction(w->trashEntryAction_,
                         QStringLiteral("user-trash-symbolic"),
                         QStyle::SP_TrashIcon,
                         MainWindow::tr("Move to Trash"),
                         QKeySequence(Qt::Key_Delete),
                         &MainWindow::trashCurrentEntry);
    configureEntryAction(w->deleteEntryPermanentlyAction_,
                         QStringLiteral("edit-delete-symbolic"),
                         QStyle::SP_DialogDiscardButton,
                         MainWindow::tr("Delete Permanently"),
                         QKeySequence(Qt::CTRL | Qt::Key_Delete),
                         &MainWindow::deleteCurrentEntryPermanently);

    w->graphTypeButton_ = new QToolButton(w->toolbar_);
    w->graphTypeButton_->setIcon(
        themedIcon(w, QStringLiteral("find-location-symbolic"), QStyle::SP_FileDialogContentsView));
    w->graphTypeButton_->setText(MainWindow::tr("Graph Type"));
    w->graphTypeButton_->setPopupMode(QToolButton::InstantPopup);
    w->graphTypeButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w->graphTypeButton_->setFocusPolicy(Qt::NoFocus);
}

void MainWindowBuilder::buildAnalysisWidgets(MainWindow *w) {
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

void MainWindowBuilder::buildScanPollTimer(MainWindow *w) {
    w->scanPollTimer_ = new QTimer(w);
    w->scanPollTimer_->setInterval(100);
    QObject::connect(w->scanPollTimer_, &QTimer::timeout, w, &MainWindow::onScanPollTick);
}

void MainWindowBuilder::buildCentralView(MainWindow *w) {
    auto *mainSplitter = new QSplitter(Qt::Vertical, w);
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

MainWindowBuilder::GraphTypeActions MainWindowBuilder::buildGraphTypeMenu(MainWindow *w) {
    auto *graphTypeMenu = new QMenu(w->graphTypeButton_);
    auto *graphTypeGroup = new QActionGroup(graphTypeMenu);
    graphTypeGroup->setExclusive(true);

    GraphTypeActions actions;
    actions.flameGraph = graphTypeMenu->addAction(MainWindow::tr("Flame Graph"));
    actions.flameGraph->setCheckable(true);
    actions.flameGraph->setChecked(true);
    graphTypeGroup->addAction(actions.flameGraph);

    actions.treeMapHeaders = graphTypeMenu->addAction(MainWindow::tr("Tree Map - Directory Headers"));
    actions.treeMapHeaders->setCheckable(true);
    graphTypeGroup->addAction(actions.treeMapHeaders);

    actions.treeMapPacked = graphTypeMenu->addAction(MainWindow::tr("Tree Map no headers"));
    actions.treeMapPacked->setCheckable(true);
    graphTypeGroup->addAction(actions.treeMapPacked);

    w->graphTypeButton_->setMenu(graphTypeMenu);
    w->graphTypeAction_ = w->toolbar_->addWidget(w->graphTypeButton_);
    w->graphTypeAction_->setVisible(false);

    return actions;
}

void MainWindowBuilder::buildHelpMenu(MainWindow *w) {
    QIcon helpIcon = themedIcon(w, QStringLiteral("help-contents-symbolic"), QStyle::SP_DialogHelpButton);

    auto *helpButton = new QToolButton(w->toolbar_);
    helpButton->setIcon(helpIcon);
    helpButton->setToolTip(MainWindow::tr("Help"));
    helpButton->setPopupMode(QToolButton::InstantPopup);
    helpButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    helpButton->setFocusPolicy(Qt::NoFocus);

    auto *helpMenu = new QMenu(helpButton);
    auto *helpAction = helpMenu->addAction(MainWindow::tr("Help"));
    QObject::connect(helpAction, &QAction::triggered, w, &MainWindow::openHelpPage);

    auto *reportIssueAction = helpMenu->addAction(MainWindow::tr("Report an Issue"));
    QObject::connect(reportIssueAction, &QAction::triggered, w, &MainWindow::reportIssue);

    helpMenu->addSeparator();

    auto *aboutAction = helpMenu->addAction(MainWindow::tr("About"));
    QObject::connect(aboutAction, &QAction::triggered, w, &MainWindow::showAboutDialog);

    helpButton->setMenu(helpMenu);
    w->toolbar_->addWidget(helpButton);
}

void MainWindowBuilder::connectCoreSignals(MainWindow *w) {
    QObject::connect(w->dirListView_, &DirListView::directorySelected, w, &MainWindow::onDirSelected);
    QObject::connect(w->dirListView_, &DirListView::entrySelected, w, &MainWindow::onEntrySelected);
    QObject::connect(w->flameGraphWidget_, &GraphWidget::entrySelected, w, &MainWindow::onGraphEntrySelected);
    QObject::connect(w->treeMapWidget_, &GraphWidget::entrySelected, w, &MainWindow::onGraphEntrySelected);
    QObject::connect(w->dirListView_, &DirListView::contextMenuRequested, w, &MainWindow::showDirListContextMenu);
    QObject::connect(w->flameGraphWidget_, &GraphWidget::contextMenuRequested, w, &MainWindow::showEntryContextMenu);
    QObject::connect(w->treeMapWidget_, &GraphWidget::contextMenuRequested, w, &MainWindow::showEntryContextMenu);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::scanRequested, w, &MainWindow::startScan);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::mountAndScanRequested, w, &MainWindow::mountAndScan);
    QObject::connect(w->welcomeWidget_, &WelcomeWidget::openDirectoryRequested, w, [w]() {
        QString dir = QFileDialog::getExistingDirectory(w, MainWindow::tr("Select Directory"), QDir::homePath());
        if (!dir.isEmpty()) w->startScan(dir);
    });
    QObject::connect(w->scanProgress_, &ScanProgressWidget::stopRequested, w, &MainWindow::onStopScan);
}

void MainWindowBuilder::connectGraphTypeSignals(MainWindow *w, const GraphTypeActions &actions) {
    QObject::connect(actions.flameGraph, &QAction::triggered, w, [w]() {
        activateGraphWidget(w, w->flameGraphWidget_, MainWindow::tr("Flame Graph"));
    });
    QObject::connect(actions.treeMapHeaders, &QAction::triggered, w, [w]() {
        w->treeMapWidget_->setRenderMode(TreeMapWidget::RenderMode::DirectoryHeaders);
        activateGraphWidget(w, w->treeMapWidget_, MainWindow::tr("Tree Map - Directory Headers"));
    });
    QObject::connect(actions.treeMapPacked, &QAction::triggered, w, [w]() {
        w->treeMapWidget_->setRenderMode(TreeMapWidget::RenderMode::Packed);
        activateGraphWidget(w, w->treeMapWidget_, MainWindow::tr("Tree Map no headers"));
    });
}

void MainWindowBuilder::connectVisibilitySignals(MainWindow *w) {
    QObject::connect(w->viewStack_, &QStackedWidget::currentChanged, w, [w](int) {
        updateGraphTypeButtonVisibility(w);
        w->updateEntryActions();
    });
    QObject::connect(w->flameStack_, &QStackedWidget::currentChanged, w, [w](int) {
        updateGraphTypeButtonVisibility(w);
        w->updateEntryActions();
    });
}

void MainWindowBuilder::updateGraphTypeButtonVisibility(MainWindow *w) {
    if (!w->graphTypeAction_) return;

    const bool graphVisible = w->viewStack_ && w->flameStack_ && w->viewStack_->currentWidget() != w->welcomeWidget_
                              && w->flameStack_->currentWidget() == w->graphTypeStack_;

    w->graphTypeAction_->setVisible(graphVisible);
}

void MainWindowBuilder::activateGraphWidget(MainWindow *w, GraphWidget *widget, const QString &label) {
    w->graphWidget_ = widget;
    w->graphTypeStack_->setCurrentWidget(widget);
    w->graphTypeButton_->setText(label);
    w->graphWidget_->setThemeColors(w->themeColors_);

    if (w->currentRoot_.valid())
        w->graphWidget_->setStores(&w->entryStore_, &w->nameStore_);
    else
        w->graphWidget_->setStores(nullptr, nullptr);

    const EntryRef focusDir = w->graphFocusDir_.valid() ? w->graphFocusDir_ : w->currentRoot_;
    w->graphWidget_->setDirectory(focusDir.valid() ? focusDir : kNoEntry);
    w->graphWidget_->setSelectedEntry(w->currentEntry_);
}

} // namespace ldirstat
