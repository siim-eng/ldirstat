#pragma once

#include <QWidget>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

class QScrollArea;
class QHBoxLayout;
class QFocusEvent;
class QKeyEvent;
class QResizeEvent;

namespace ldirstat {

class DirListColumn;

class DirListView : public QWidget {
    Q_OBJECT

public:
    explicit DirListView(QWidget* parent = nullptr);

    bool handleArrowKey(int key);
    void setThemeColors(const ThemeColors& colors);
    void setRoot(const DirEntryStore& store, const NameStore& names, EntryRef root);
    void selectEntry(EntryRef ref);
    void refreshAfterRemoval(EntryRef removedRef, EntryRef parentRef);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void directorySelected(ldirstat::EntryRef ref);
    void entrySelected(ldirstat::EntryRef ref);
    void contextMenuRequested(ldirstat::EntryRef ref, QPoint globalPos);

private slots:
    void onColumnEntryClicked(ldirstat::EntryRef ref, bool isDir);
    void onColumnContextMenuRequested(ldirstat::EntryRef ref, QPoint globalPos);

private:
    void applySelectionInColumn(int columnIndex, int rowIndex);
    void addColumn(EntryRef dirRef);
    void enterRootFocus(bool emitSelection = true);
    int indexOfColumn(const DirListColumn* column) const;
    void setActiveColumnIndex(int columnIndex);
    void truncateColumnsAfter(int columnIndex);
    void updateActiveColumnState();
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
    int activeColumnIndex_ = -1;
    bool rootFocused_ = true;
    int columnWidth_ = 0;
};

} // namespace ldirstat
