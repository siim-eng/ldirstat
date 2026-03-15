#pragma once

#include <QMainWindow>
#include <QThread>

#include "direntrystore.h"
#include "filesystem.h"
#include "flamegraph.h"
#include "namestore.h"
#include "scanner.h"

class QStackedWidget;
class QSplitter;
class QTimer;

namespace ldirstat {

class DirTreeView;
class FileListView;
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

signals:
    void scanComplete(EntryRef root);

private slots:
    void onOpenDirectory();
    void onScanFinished(EntryRef root);
    void onDirSelected(EntryRef ref);
    void onFlameRectClicked(EntryRef ref);
    void onScanPollTick();
    void onStopScan();

private slots:
    void startScan(const QString& path);

private:

    QThread* scanThread_ = nullptr;

    // Core state.
    FileSystems fileSystems_;
    DirEntryStore entryStore_;
    NameStore nameStore_;
    Scanner scanner_;
    FlameGraph flameGraph_;

    EntryRef currentRoot_;
    EntryRef selectedDir_;

    // UI widgets (created by builder, parented to this).
    QStackedWidget* viewStack_ = nullptr;
    WelcomeWidget* welcomeWidget_ = nullptr;
    DirTreeView* dirTree_ = nullptr;
    FileListView* fileList_ = nullptr;
    QStackedWidget* flameStack_ = nullptr;
    FlameGraphWidget* flameGraphWidget_ = nullptr;
    ScanProgressWidget* scanProgress_ = nullptr;
    QTimer* scanPollTimer_ = nullptr;
};

} // namespace ldirstat
