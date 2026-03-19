#pragma once

#include <QMainWindow>
#include <QThread>

#include "direntrystore.h"
#include "filesystem.h"
#include "namestore.h"
#include "scanner.h"
#include "themecolors.h"

class QAction;
class QProcess;
class QStackedWidget;
class QSplitter;
class QTimer;
class QToolBar;
class QToolButton;

namespace ldirstat {

class DirListView;
class FlameGraphWidget;
class GraphWidget;
class MainWindowBuilder;
class ScanProgressWidget;
class TreeMapWidget;
class WelcomeWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class MainWindowBuilder;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void scanComplete(EntryRef root);

private slots:
    void onOverview();
    void onRescan();
    void onScanFinished(EntryRef root);
    void onDirSelected(EntryRef ref);
    void onEntrySelected(EntryRef ref);
    void onGraphEntrySelected(EntryRef ref);
    void onScanPollTick();
    void onStopScan();
    void openCurrentEntry();
    void openCurrentEntryTerminal();
    void copyCurrentEntryPath();
    void trashCurrentEntry();
    void startScan(const QString& path);
    void mountAndScan(const QString& devicePath);
    void showEntryContextMenu(EntryRef ref, QPoint globalPos);

private:
    void refreshWelcomeVolumes();
    void setMountInProgress(bool inProgress, const QString& status = {});
    bool isGraphPageVisible() const;
    bool shouldForwardDirListArrowKey(QObject* watched, QEvent* event) const;
    EntryRef graphFocusForEntry(EntryRef ref) const;
    bool isEntryInSubtree(EntryRef ref, EntryRef ancestor) const;
    void setCurrentEntry(EntryRef ref);
    void syncGraphHighlight();
    void syncGraphSelection();
    void updateEntryActions();
    QString pathForEntry(EntryRef ref) const;

    QThread* scanThread_ = nullptr;
    QProcess* mountProcess_ = nullptr;
    QString lastScanPath_;

    // Core state.
    FileSystems fileSystems_;
    DirEntryStore entryStore_;
    NameStore nameStore_;
    Scanner scanner_;

    EntryRef currentRoot_ = kNoEntry;
    EntryRef currentEntry_ = kNoEntry;
    EntryRef graphFocusDir_ = kNoEntry;
    ThemeColors themeColors_;

    // UI widgets (created by builder, parented to this).
    QToolBar* toolbar_ = nullptr;
    QAction* overviewAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QAction* openEntryAction_ = nullptr;
    QAction* openEntryTerminalAction_ = nullptr;
    QAction* copyEntryPathAction_ = nullptr;
    QAction* trashEntryAction_ = nullptr;
    QToolButton* graphTypeButton_ = nullptr;
    QAction* graphTypeAction_ = nullptr;
    QStackedWidget* viewStack_ = nullptr;
    WelcomeWidget* welcomeWidget_ = nullptr;
    DirListView* dirListView_ = nullptr;
    QStackedWidget* flameStack_ = nullptr;
    QStackedWidget* graphTypeStack_ = nullptr;
    FlameGraphWidget* flameGraphWidget_ = nullptr;
    TreeMapWidget* treeMapWidget_ = nullptr;
    GraphWidget* graphWidget_ = nullptr;
    ScanProgressWidget* scanProgress_ = nullptr;
    QTimer* scanPollTimer_ = nullptr;
};

} // namespace ldirstat
