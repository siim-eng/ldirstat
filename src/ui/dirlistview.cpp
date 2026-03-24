#include "dirlistview.h"
#include "dirlistcolumn.h"

#include <QApplication>
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

    connect(scrollArea_->horizontalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { syncColumnHeights(); });
}

std::vector<EntryRef> DirListView::selectedEntries() const {
    if (activeColumnIndex_ < 0 || activeColumnIndex_ >= static_cast<int>(columns_.size()))
        return {};
    return columns_[activeColumnIndex_]->selectedRefs();
}

void DirListView::setThemeColors(const ThemeColors& colors) {
    themeColors_ = colors;
    for (auto* col : columns_)
        col->setThemeColors(colors);
}

bool DirListView::handleArrowKey(int key, Qt::KeyboardModifiers modifiers) {
    const Qt::KeyboardModifiers supportedModifiers =
        Qt::ShiftModifier | Qt::ControlModifier;
    if ((modifiers & ~supportedModifiers) != Qt::NoModifier)
        return false;

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
        applyFocusInColumn(0, targetRow, modifiers,
                           modifiers.testFlag(Qt::ControlModifier) &&
                           !modifiers.testFlag(Qt::ShiftModifier));
        return true;
    }

    DirListColumn* column = columns_[activeColumnIndex_];
    if (!column)
        return true;

    if (key == Qt::Key_Left) {
        if (modifiers != Qt::NoModifier)
            return true;
        if (activeColumnIndex_ == 0) {
            enterRootFocus();
            return true;
        }

        const int focusedColumnIndex = activeColumnIndex_;
        const EntryRef focusedDir = columns_[focusedColumnIndex]->dirRef();
        const EntryRef parentDir = (*store_)[focusedDir].parent;
        const int parentColumnIndex = focusedColumnIndex - 1;

        suppressColumnFocusChanges_ = true;
        truncateColumnsAfter(focusedColumnIndex);
        setActiveColumnIndex(parentColumnIndex);
        if (focusedColumnIndex < static_cast<int>(columns_.size()))
            columns_[focusedColumnIndex]->clearFocus();
        rootFocused_ = false;
        columns_[parentColumnIndex]->setFocusedRef(focusedDir, true);
        syncPathHighlights();
        suppressColumnFocusChanges_ = false;

        emit entrySelected(focusedDir);
        emit directorySelected(parentDir.valid() ? parentDir : focusedDir);
        return true;
    }

    if (key == Qt::Key_Right) {
        if (modifiers != Qt::NoModifier)
            return true;
        const int focusedRow = column->focusedIndex();
        if (focusedRow < 0 || !column->rowIsDir(focusedRow))
            return true;
        if (activeColumnIndex_ + 1 >= static_cast<int>(columns_.size()))
            return true;

        DirListColumn* nextColumn = columns_[activeColumnIndex_ + 1];
        if (nextColumn->rowCount() > 0)
            applyFocusInColumn(activeColumnIndex_ + 1, 0);
        else {
            setActiveColumnIndex(activeColumnIndex_ + 1);
            rootFocused_ = false;
        }
        return true;
    }

    if (column->rowCount() == 0)
        return true;

    int focusedRow = column->focusedIndex();
    if (focusedRow < 0)
        focusedRow = key == Qt::Key_Up ? column->rowCount() - 1 : 0;
    else if (key == Qt::Key_Up)
        focusedRow = std::max(0, focusedRow - 1);
    else
        focusedRow = std::min(column->rowCount() - 1, focusedRow + 1);

    applyFocusInColumn(activeColumnIndex_, focusedRow, modifiers,
                       modifiers.testFlag(Qt::ControlModifier) &&
                       !modifiers.testFlag(Qt::ShiftModifier));
    return true;
}

void DirListView::setRoot(const DirEntryStore& store, const NameStore& names,
                          EntryRef root) {
    store_ = &store;
    names_ = &names;
    rootSize_ = store[root].size;

    suppressColumnFocusChanges_ = true;
    truncateColumnsAfter(-1);
    addColumn(root);
    syncPathHighlights();
    suppressColumnFocusChanges_ = false;

    rootFocused_ = true;
    activeColumnIndex_ = 0;
    updateActiveColumnState();
}

void DirListView::selectEntry(EntryRef ref) {
    if (!store_ || !ref.valid())
        return;

    std::vector<EntryRef> path;
    EntryRef cur = ref;
    while (cur.valid()) {
        path.push_back(cur);
        cur = (*store_)[cur].parent;
    }
    std::reverse(path.begin(), path.end());

    if (path.empty())
        return;

    rootSize_ = (*store_)[path.front()].size;

    suppressColumnFocusChanges_ = true;
    truncateColumnsAfter(-1);

    for (EntryRef pathRef : path) {
        const DirEntry& entry = (*store_)[pathRef];
        if (entry.isDir())
            addColumn(pathRef);
    }

    syncPathHighlights();

    int focusColumnIndex = -1;
    if (ref != path.front()) {
        if ((*store_)[ref].isDir())
            focusColumnIndex = static_cast<int>(columns_.size()) - 2;
        else
            focusColumnIndex = static_cast<int>(columns_.size()) - 1;
    }

    if (focusColumnIndex >= 0 && focusColumnIndex < static_cast<int>(columns_.size())) {
        columns_[focusColumnIndex]->setFocusedRef(ref, true);
        activeColumnIndex_ = focusColumnIndex;
        rootFocused_ = false;
    } else {
        activeColumnIndex_ = columns_.empty() ? -1 : 0;
        rootFocused_ = true;
        if (!columns_.empty()) {
            columns_[0]->clearSelection();
            columns_[0]->clearFocus();
        }
    }

    suppressColumnFocusChanges_ = false;
    updateActiveColumnState();
    scrollToLastColumn();

    if (focusColumnIndex >= 0) {
        emit entrySelected(ref);
        const EntryRef parent = (*store_)[ref].parent;
        emit directorySelected(parent.valid() ? parent : ref);
    } else if (!columns_.empty()) {
        emit entrySelected(kNoEntry);
        emit directorySelected(columns_[0]->dirRef());
    }
}

void DirListView::keyPressEvent(QKeyEvent* event) {
    if (handleArrowKey(event->key(), event->modifiers())) {
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

void DirListView::onColumnActivated() {
    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    const int colIndex = indexOfColumn(col);
    if (colIndex < 0)
        return;

    setActiveColumnIndex(colIndex);
    rootFocused_ = !col->focusedRef().valid() && colIndex == 0;
    updateActiveColumnState();
}

void DirListView::onColumnFocusChanged(EntryRef ref, bool isDir) {
    if (suppressColumnFocusChanges_)
        return;

    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    const int colIndex = indexOfColumn(col);
    if (colIndex < 0)
        return;

    QWidget* focusWidget = QApplication::focusWidget();
    if (focusWidget == nullptr || !col->isAncestorOf(focusWidget))
        setFocus(Qt::MouseFocusReason);
    setActiveColumnIndex(colIndex);
    rootFocused_ = !ref.valid() && colIndex == 0;

    truncateColumnsAfter(colIndex);
    if (ref.valid() && isDir) {
        addColumn(ref);
        scrollToLastColumn();
        rootFocused_ = false;
    }

    syncPathHighlights();

    if (ref.valid()) {
        emit entrySelected(ref);

        EntryRef focusDir = col->dirRef();
        if (const EntryRef parent = (*store_)[ref].parent; parent.valid())
            focusDir = parent;
        else
            focusDir = ref;

        emit directorySelected(focusDir);
        return;
    }

    emit entrySelected(kNoEntry);
    emit directorySelected(col->dirRef());
}

void DirListView::onColumnContextMenuRequested(EntryRef ref, QPoint globalPos) {
    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    const int colIndex = indexOfColumn(col);
    if (colIndex >= 0) {
        setActiveColumnIndex(colIndex);
        rootFocused_ = false;
    }

    emit contextMenuRequested(ref, globalPos);
}

void DirListView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncColumnHeights();
}

void DirListView::applyFocusInColumn(int columnIndex, int rowIndex,
                                     Qt::KeyboardModifiers modifiers,
                                     bool preserveSelection) {
    if (columnIndex < 0 || columnIndex >= static_cast<int>(columns_.size()))
        return;

    DirListColumn* column = columns_[columnIndex];
    if (!column || rowIndex < 0 || rowIndex >= column->rowCount())
        return;

    setActiveColumnIndex(columnIndex);
    rootFocused_ = false;
    column->applySelectionAtRow(rowIndex, modifiers, preserveSelection);
}

void DirListView::addColumn(EntryRef dirRef) {
    auto* col = new DirListColumn(*store_, *names_, dirRef, rootSize_,
                                  themeColors_, columnWidth_, scrollContent_);

    const int insertIndex = columnsLayout_->count() - 1;
    columnsLayout_->insertWidget(insertIndex, col);
    columns_.push_back(col);

    connect(col, &DirListColumn::activated,
            this, &DirListView::onColumnActivated);
    connect(col, &DirListColumn::focusChanged,
            this, &DirListView::onColumnFocusChanged);
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
    columns_[0]->clearFocus();
    syncPathHighlights();
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

    const int clamped =
        std::clamp(columnIndex, 0, static_cast<int>(columns_.size()) - 1);
    if (activeColumnIndex_ >= 0 &&
        activeColumnIndex_ < static_cast<int>(columns_.size()) &&
        activeColumnIndex_ != clamped) {
        columns_[activeColumnIndex_]->clearSelection();
    }

    activeColumnIndex_ = clamped;
    updateActiveColumnState();
}

void DirListView::syncPathHighlights() {
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
        const EntryRef nextDir =
            i + 1 < static_cast<int>(columns_.size()) ? columns_[i + 1]->dirRef() : kNoEntry;
        columns_[i]->setPathRef(nextDir);
    }
}

void DirListView::truncateColumnsAfter(int columnIndex) {
    const int startRemove = columnIndex + 1;
    for (int i = static_cast<int>(columns_.size()) - 1; i >= startRemove; --i)
        delete columns_[i];

    if (startRemove < static_cast<int>(columns_.size()))
        columns_.erase(columns_.begin() + startRemove, columns_.end());

    if (columns_.empty()) {
        activeColumnIndex_ = -1;
        rootFocused_ = true;
    } else {
        activeColumnIndex_ = std::clamp(activeColumnIndex_, 0,
                                        static_cast<int>(columns_.size()) - 1);
    }

    syncPathHighlights();
    updateActiveColumnState();
    syncColumnHeights();
    QTimer::singleShot(0, this, [this]() { syncColumnHeights(); });
}

void DirListView::updateActiveColumnState() {
    const QWidget* focusWidget = QApplication::focusWidget();
    const bool showActiveColumn =
        activeColumnIndex_ >= 0 &&
        (hasFocus() || (focusWidget != nullptr && isAncestorOf(const_cast<QWidget*>(focusWidget))));

    for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
        columns_[i]->setKeyboardActive(showActiveColumn && i == activeColumnIndex_);
}

void DirListView::syncColumnHeights() {
    const int h = scrollArea_->viewport()->height();
    for (auto* col : columns_)
        col->setFixedHeight(h);

    const int spacing = columnsLayout_->spacing();
    const int n = static_cast<int>(columns_.size());
    const int totalWidth = n * columnWidth_ + (n > 1 ? (n - 1) * spacing : 0);
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
    const int sizeField = fm.horizontalAdvance("9999 MB");
    const int pctField = fm.horizontalAdvance("100%");
    const int nameField = fm.horizontalAdvance("directoryname123");
    const int arrowSpace = DirListColumn::kArrowSize * 2 + DirListColumn::kPadding;
    const int scrollBarW = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    return DirListColumn::kLeftPadding + sizeField + DirListColumn::kPadding
         + pctField + DirListColumn::kPadding + nameField + arrowSpace
         + DirListColumn::kPadding + scrollBarW;
}

} // namespace ldirstat
