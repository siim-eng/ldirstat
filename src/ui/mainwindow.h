#pragma once

#include <QMainWindow>
#include <QThread>

#include "direntrystore.h"
#include "filesystem.h"
#include "flamegraph.h"
#include "namestore.h"
#include "scanner.h"
#include "themecolors.h"

class QAction;
class QStackedWidget;
class QSplitter;
class QTimer;
class QToolBar;

namespace ldirstat {

class DirListView;
class FlameGraphWidget;
class MainWindowBuilder;
class ScanProgressWidget;
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
    void onFlameRectClicked(EntryRef ref);
    void onScanPollTick();
    void onStopScan();
    void startScan(const QString& path);

private:

    QThread* scanThread_ = nullptr;
    QString lastScanPath_;

    // Core state.
    FileSystems fileSystems_;
    DirEntryStore entryStore_;
    NameStore nameStore_;
    Scanner scanner_;
    FlameGraph flameGraph_;

    EntryRef currentRoot_;
    EntryRef selectedDir_;
    ThemeColors themeColors_;

    // UI widgets (created by builder, parented to this).
    QToolBar* toolbar_ = nullptr;
    QAction* overviewAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QStackedWidget* viewStack_ = nullptr;
    WelcomeWidget* welcomeWidget_ = nullptr;
    DirListView* dirListView_ = nullptr;
    QStackedWidget* flameStack_ = nullptr;
    FlameGraphWidget* flameGraphWidget_ = nullptr;
    ScanProgressWidget* scanProgress_ = nullptr;
    QTimer* scanPollTimer_ = nullptr;
};

} // namespace ldirstat
