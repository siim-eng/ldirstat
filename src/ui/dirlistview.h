#pragma once

#include <QWidget>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

class QScrollArea;
class QHBoxLayout;

namespace ldirstat {

class DirListColumn;

class DirListView : public QWidget {
    Q_OBJECT

public:
    explicit DirListView(QWidget* parent = nullptr);

    void setThemeColors(const ThemeColors& colors);
    void setRoot(const DirEntryStore& store, const NameStore& names, EntryRef root);
    void selectEntry(EntryRef ref);
    void refreshAfterRemoval(EntryRef removedRef, EntryRef parentRef);

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void directorySelected(ldirstat::EntryRef ref);
    void contextMenuRequested(ldirstat::EntryRef ref, QPoint globalPos);

private slots:
    void onColumnEntryClicked(ldirstat::EntryRef ref, bool isDir);

private:
    void addColumn(EntryRef dirRef);
    void truncateColumnsAfter(int columnIndex);
    void scrollToLastColumn();
    void syncColumnHeights();
    int computeColumnWidth() const;

    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
    uint64_t rootSize_ = 0;
    ThemeColors themeColors_;

    QScrollArea* scrollArea_;
    QWidget* scrollContent_;
    QHBoxLayout* columnsLayout_;
    std::vector<DirListColumn*> columns_;
    int columnWidth_ = 0;
};

} // namespace ldirstat
