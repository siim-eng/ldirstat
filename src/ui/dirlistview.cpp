#include "dirlistview.h"
#include "dirlistcolumn.h"

#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace ldirstat {

DirListView::DirListView(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFocusPolicy(Qt::NoFocus);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(false);

    scrollContent_ = new QWidget();
    columnsLayout_ = new QHBoxLayout(scrollContent_);
    columnsLayout_->setContentsMargins(0, 0, 0, 0);
    columnsLayout_->setSpacing(2);
    columnsLayout_->addStretch();

    scrollArea_->setWidget(scrollContent_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea_);

    columnWidth_ = computeColumnWidth();

    // When horizontal scrollbar appears/disappears, viewport height changes.
    connect(scrollArea_->horizontalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { syncColumnHeights(); });
}

void DirListView::setThemeColors(const ThemeColors& colors) {
    themeColors_ = colors;
    for (auto* col : columns_)
        col->setThemeColors(colors);
}

bool DirListView::handleArrowKey(int key) {
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
        break;
    default:
        return false;
    }

    if (!hasFocus())
        setFocus(Qt::OtherFocusReason);

    if (columns_.empty())
        return true;

    if (activeColumnIndex_ < 0 || activeColumnIndex_ >= static_cast<int>(columns_.size()))
        activeColumnIndex_ = 0;

    if (rootFocused_) {
        if (key == Qt::Key_Left)
            return true;

        DirListColumn* rootColumn = columns_.front();
        if (rootColumn->rowCount() == 0)
            return true;

        const int targetRow = key == Qt::Key_Up ? rootColumn->rowCount() - 1 : 0;
        applySelectionInColumn(0, targetRow);
        return true;
    }

    DirListColumn* column = columns_[activeColumnIndex_];
    if (!column)
        return true;

    if (key == Qt::Key_Left) {
        if (activeColumnIndex_ == 0) {
            enterRootFocus();
            return true;
        }
        const int focusedColumnIndex = activeColumnIndex_;
        const EntryRef focusedDir = columns_[focusedColumnIndex]->dirRef();
        const EntryRef parentDir = (*store_)[focusedDir].parent;
        const int parentColumnIndex = focusedColumnIndex - 1;
        const int lastColumnIndex = static_cast<int>(columns_.size()) - 1;
        const int selectedRow = column->selectedIndex();
        const bool hasFileSelection =
            selectedRow >= 0 && !column->rowIsDir(selectedRow);
        const int keepThroughColumn =
            (focusedColumnIndex == lastColumnIndex && !hasFileSelection)
                ? parentColumnIndex
                : focusedColumnIndex;

        columns_[parentColumnIndex]->setSelectedRef(focusedDir);
        truncateColumnsAfter(keepThroughColumn);

        if (keepThroughColumn == focusedColumnIndex &&
            focusedColumnIndex < static_cast<int>(columns_.size())) {
            columns_[focusedColumnIndex]->clearSelection();
        }

        setActiveColumnIndex(parentColumnIndex);

        rootFocused_ = false;
        emit entrySelected(focusedDir);
        emit directorySelected(parentDir.valid() ? parentDir : focusedDir);
        return true;
    }

    if (key == Qt::Key_Right) {
        const int selectedRow = column->selectedIndex();
        if (selectedRow < 0 || !column->rowIsDir(selectedRow))
            return true;
        if (activeColumnIndex_ + 1 >= static_cast<int>(columns_.size()))
            return true;

        DirListColumn* nextColumn = columns_[activeColumnIndex_ + 1];
        if (nextColumn->rowCount() > 0)
            applySelectionInColumn(activeColumnIndex_ + 1, 0);
        else
            setActiveColumnIndex(activeColumnIndex_ + 1);
        return true;
    }

    if (column->rowCount() == 0)
        return true;

    int selectedRow = column->selectedIndex();
    if (selectedRow < 0)
        selectedRow = key == Qt::Key_Up ? column->rowCount() - 1 : 0;
    else if (key == Qt::Key_Up)
        selectedRow = std::max(0, selectedRow - 1);
    else
        selectedRow = std::min(column->rowCount() - 1, selectedRow + 1);

    if (selectedRow != column->selectedIndex())
        applySelectionInColumn(activeColumnIndex_, selectedRow);

    return true;
}

void DirListView::setRoot(const DirEntryStore& store, const NameStore& names,
                          EntryRef root) {
    store_ = &store;
    names_ = &names;
    rootSize_ = store[root].size;

    truncateColumnsAfter(-1);
    addColumn(root);
    rootFocused_ = true;
    activeColumnIndex_ = 0;
    updateActiveColumnState();
}

void DirListView::selectEntry(EntryRef ref) {
    if (!store_ || !ref.valid())
        return;

    // Build path from root to ref by walking parent chain.
    std::vector<EntryRef> path;
    EntryRef cur = ref;
    while (cur.valid()) {
        path.push_back(cur);
        cur = (*store_)[cur].parent;
    }
    std::reverse(path.begin(), path.end());

    // path[0] should be the scan root. Rebuild columns.
    truncateColumnsAfter(-1);

    int deepestSelectedColumn = -1;
    for (size_t i = 0; i < path.size(); ++i) {
        const DirEntry& entry = (*store_)[path[i]];
        if (entry.isDir()) {
            addColumn(path[i]);
            // Select the next entry in the path within this column.
            if (i + 1 < path.size()) {
                columns_.back()->setSelectedRef(path[i + 1]);
                if (columns_.back()->selectedIndex() >= 0)
                    deepestSelectedColumn = static_cast<int>(columns_.size()) - 1;
            }
        }
    }

    if (deepestSelectedColumn >= 0) {
        rootFocused_ = false;
        setActiveColumnIndex(deepestSelectedColumn);
    } else {
        enterRootFocus(false);
    }

    scrollToLastColumn();
}

void DirListView::refreshAfterRemoval(EntryRef removedRef, EntryRef parentRef) {
    if (!store_ || columns_.empty())
        return;

    std::vector<EntryRef> selectedRefs;
    selectedRefs.reserve(columns_.size());
    for (auto* column : columns_)
        selectedRefs.push_back(column->selectedRef());

    const int previousActiveColumn = activeColumnIndex_;

    // Find the column showing the removed entry or its parent, and truncate
    // any child columns beyond it.
    int rebuildFrom = -1;
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
        if (columns_[i]->dirRef() == removedRef) {
            truncateColumnsAfter(i - 1);
            rebuildFrom = static_cast<int>(columns_.size()) - 1;
            break;
        }
        if (columns_[i]->dirRef() == parentRef) {
            truncateColumnsAfter(i);
            rebuildFrom = i;
            break;
        }
    }

    if (rebuildFrom < 0)
        rebuildFrom = static_cast<int>(columns_.size()) - 1;

    // Rebuild the affected column and all ancestors so their cached
    // sizes, percentages and footer stats reflect the removal.
    if (!columns_.empty())
        rootSize_ = (*store_)[columns_[0]->dirRef()].size;

    for (int i = std::max(rebuildFrom, 0); i >= 0; --i)
        columns_[i]->rebuild(rootSize_);

    int deepestSelectedColumn = -1;
    for (int i = 0; i < static_cast<int>(columns_.size()) && i < static_cast<int>(selectedRefs.size());
         ++i) {
        if (!selectedRefs[i].valid())
            break;

        columns_[i]->setSelectedRef(selectedRefs[i]);
        if (columns_[i]->selectedRef() != selectedRefs[i]) {
            truncateColumnsAfter(i);
            break;
        }

        deepestSelectedColumn = i;
    }

    if (deepestSelectedColumn >= 0) {
        rootFocused_ = false;
        setActiveColumnIndex(std::min(previousActiveColumn,
                                      static_cast<int>(columns_.size()) - 1));
    } else {
        enterRootFocus(false);
    }
}

void DirListView::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::NoModifier && handleArrowKey(event->key())) {
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void DirListView::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    updateActiveColumnState();
}

void DirListView::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    updateActiveColumnState();
}

void DirListView::onColumnEntryClicked(EntryRef /*ref*/, bool /*isDir*/) {
    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    const int colIndex = indexOfColumn(col);
    if (colIndex < 0)
        return;

    setFocus(Qt::MouseFocusReason);
    rootFocused_ = false;
    setActiveColumnIndex(colIndex);
    applySelectionInColumn(colIndex, col->selectedIndex());
}

void DirListView::onColumnContextMenuRequested(EntryRef ref, QPoint globalPos) {
    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    const int colIndex = indexOfColumn(col);
    if (colIndex >= 0) {
        setFocus(Qt::MouseFocusReason);
        rootFocused_ = false;
        setActiveColumnIndex(colIndex);
    }

    emit entrySelected(ref);
    emit contextMenuRequested(ref, globalPos);
}

void DirListView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncColumnHeights();
}

void DirListView::applySelectionInColumn(int columnIndex, int rowIndex) {
    if (columnIndex < 0 || columnIndex >= static_cast<int>(columns_.size()))
        return;

    DirListColumn* column = columns_[columnIndex];
    if (!column || rowIndex < 0 || rowIndex >= column->rowCount())
        return;

    const EntryRef ref = column->refAtRow(rowIndex);
    const bool isDir = column->rowIsDir(rowIndex);

    truncateColumnsAfter(columnIndex);
    column = columns_[columnIndex];
    column->setSelectedIndex(rowIndex);
    rootFocused_ = false;
    setActiveColumnIndex(columnIndex);
    emit entrySelected(ref);
    EntryRef focusDir = column->dirRef();
    if (const EntryRef parent = (*store_)[ref].parent; parent.valid())
        focusDir = parent;
    else if (ref.valid())
        focusDir = ref;

    if (isDir) {
        addColumn(ref);
        scrollToLastColumn();
    }

    emit directorySelected(focusDir);
}

void DirListView::addColumn(EntryRef dirRef) {
    auto* col = new DirListColumn(*store_, *names_, dirRef, rootSize_,
                                  themeColors_, columnWidth_, scrollContent_);

    // Insert before the stretch.
    int insertIndex = columnsLayout_->count() - 1;
    columnsLayout_->insertWidget(insertIndex, col);
    columns_.push_back(col);

    connect(col, &DirListColumn::entryClicked,
            this, &DirListView::onColumnEntryClicked);
    connect(col, &DirListColumn::contextMenuRequested,
            this, &DirListView::onColumnContextMenuRequested);

    syncColumnHeights();
    updateActiveColumnState();
    QTimer::singleShot(0, this, [this]() { syncColumnHeights(); });
}

void DirListView::enterRootFocus(bool emitSelection) {
    if (columns_.empty())
        return;

    truncateColumnsAfter(0);
    columns_[0]->clearSelection();
    rootFocused_ = true;
    setActiveColumnIndex(0);
    scrollArea_->horizontalScrollBar()->setValue(0);

    if (emitSelection) {
        emit entrySelected(kNoEntry);
        emit directorySelected(columns_[0]->dirRef());
    }
}

int DirListView::indexOfColumn(const DirListColumn* column) const {
    auto it = std::find(columns_.begin(), columns_.end(), column);
    if (it == columns_.end())
        return -1;
    return static_cast<int>(std::distance(columns_.begin(), it));
}

void DirListView::setActiveColumnIndex(int columnIndex) {
    if (columns_.empty()) {
        activeColumnIndex_ = -1;
        updateActiveColumnState();
        return;
    }

    activeColumnIndex_ = std::clamp(columnIndex, 0, static_cast<int>(columns_.size()) - 1);
    updateActiveColumnState();
}

void DirListView::truncateColumnsAfter(int columnIndex) {
    int startRemove = columnIndex + 1;
    for (int i = static_cast<int>(columns_.size()) - 1; i >= startRemove; --i) {
        delete columns_[i];
    }
    if (startRemove < static_cast<int>(columns_.size()))
        columns_.erase(columns_.begin() + startRemove, columns_.end());

    if (columns_.empty()) {
        activeColumnIndex_ = -1;
        rootFocused_ = true;
    } else {
        activeColumnIndex_ = std::clamp(activeColumnIndex_, 0,
                                        static_cast<int>(columns_.size()) - 1);
    }

    updateActiveColumnState();
    syncColumnHeights();
    QTimer::singleShot(0, this, [this]() { syncColumnHeights(); });
}

void DirListView::updateActiveColumnState() {
    const bool showActiveColumn = hasFocus() && activeColumnIndex_ >= 0;
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
        columns_[i]->setKeyboardActive(showActiveColumn && i == activeColumnIndex_);
}

void DirListView::syncColumnHeights() {
    int h = scrollArea_->viewport()->height();
    for (auto* col : columns_)
        col->setFixedHeight(h);

    int spacing = columnsLayout_->spacing();
    int n = static_cast<int>(columns_.size());
    int totalWidth = n * columnWidth_ + (n > 1 ? (n - 1) * spacing : 0);
    scrollContent_->setFixedWidth(std::max(totalWidth, scrollArea_->viewport()->width()));
    scrollContent_->setFixedHeight(h);
}

void DirListView::scrollToLastColumn() {
    QTimer::singleShot(0, this, [this]() {
        if (!columns_.empty())
            scrollArea_->ensureWidgetVisible(columns_.back());
    });
}

int DirListView::computeColumnWidth() const {
    QFontMetrics fm(font());
    int sizeField = fm.horizontalAdvance("9999 MB");
    int pctField = fm.horizontalAdvance("100%");
    int nameField = fm.horizontalAdvance("directoryname123");
    int arrowSpace = DirListColumn::kArrowSize * 2 + DirListColumn::kPadding;
    int scrollBarW = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    return DirListColumn::kLeftPadding + sizeField + DirListColumn::kPadding
         + pctField + DirListColumn::kPadding + nameField + arrowSpace
         + DirListColumn::kPadding + scrollBarW;
}

} // namespace ldirstat
