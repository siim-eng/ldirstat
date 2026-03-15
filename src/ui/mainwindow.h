#pragma once

#include <QMainWindow>
#include <QThread>

#include "direntrystore.h"
#include "flamegraph.h"
#include "namestore.h"
#include "scanner.h"

class QSplitter;

namespace ldirstat {

class DirTreeView;
class FileListView;
class FlameGraphWidget;
class MainWindowBuilder;

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

private:
    void startScan(const QString& path);

    QThread* scanThread_ = nullptr;

    // Core state.
    DirEntryStore entryStore_;
    NameStore nameStore_;
    FlameGraph flameGraph_;

    EntryRef currentRoot_;
    EntryRef selectedDir_;

    // UI widgets (created by builder, parented to this).
    DirTreeView* dirTree_ = nullptr;
    FileListView* fileList_ = nullptr;
    FlameGraphWidget* flameGraphWidget_ = nullptr;
};

} // namespace ldirstat
