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

signals:
    void scanComplete(EntryRef root);

private slots:
    void onOverview();
    void onRescan();
    void onScanFinished(EntryRef root);
    void onDirSelected(EntryRef ref);
    void onGraphEntrySelected(EntryRef ref);
    void onScanPollTick();
    void onStopScan();
    void startScan(const QString& path);
    void mountAndScan(const QString& devicePath);
    void showEntryContextMenu(EntryRef ref, QPoint globalPos);

private:
    void refreshWelcomeVolumes();
    void setMountInProgress(bool inProgress, const QString& status = {});

    QThread* scanThread_ = nullptr;
    QProcess* mountProcess_ = nullptr;
    QString lastScanPath_;

    // Core state.
    FileSystems fileSystems_;
    DirEntryStore entryStore_;
    NameStore nameStore_;
    Scanner scanner_;

    EntryRef currentRoot_;
    EntryRef selectedDir_;
    ThemeColors themeColors_;

    // UI widgets (created by builder, parented to this).
    QToolBar* toolbar_ = nullptr;
    QAction* overviewAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QToolButton* graphTypeButton_ = nullptr;
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
