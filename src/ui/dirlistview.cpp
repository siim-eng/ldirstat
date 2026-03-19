#include "dirlistview.h"
#include "dirlistcolumn.h"

#include <QHBoxLayout>
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
    scrollArea_ = new QScrollArea(this);
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

void DirListView::setRoot(const DirEntryStore& store, const NameStore& names,
                          EntryRef root) {
    store_ = &store;
    names_ = &names;
    rootSize_ = store[root].size;

    truncateColumnsAfter(-1);
    addColumn(root);
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

    for (size_t i = 0; i < path.size(); ++i) {
        const DirEntry& entry = (*store_)[path[i]];
        if (entry.isDir()) {
            addColumn(path[i]);
            // Select the next entry in the path within this column.
            if (i + 1 < path.size())
                columns_.back()->setSelectedRef(path[i + 1]);
        }
    }

    scrollToLastColumn();
}

void DirListView::refreshAfterRemoval(EntryRef removedRef, EntryRef parentRef) {
    if (!store_ || columns_.empty())
        return;

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

    // Rebuild the affected column and all ancestors so their cached
    // sizes, percentages and footer stats reflect the removal.
    if (!columns_.empty())
        rootSize_ = (*store_)[columns_[0]->dirRef()].size;

    for (int i = std::max(rebuildFrom, 0); i >= 0; --i)
        columns_[i]->rebuild(rootSize_);
}

void DirListView::onColumnEntryClicked(EntryRef ref, bool isDir) {
    auto* col = qobject_cast<DirListColumn*>(sender());
    if (!col)
        return;

    auto it = std::find(columns_.begin(), columns_.end(), col);
    if (it == columns_.end())
        return;

    int colIndex = static_cast<int>(std::distance(columns_.begin(), it));
    truncateColumnsAfter(colIndex);

    if (isDir) {
        addColumn(ref);
        scrollToLastColumn();
        emit directorySelected(ref);
    }
}

void DirListView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncColumnHeights();
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
            this, &DirListView::contextMenuRequested);

    syncColumnHeights();
    QTimer::singleShot(0, this, [this]() { syncColumnHeights(); });
}

void DirListView::truncateColumnsAfter(int columnIndex) {
    int startRemove = columnIndex + 1;
    for (int i = static_cast<int>(columns_.size()) - 1; i >= startRemove; --i) {
        delete columns_[i];
    }
    if (startRemove < static_cast<int>(columns_.size()))
        columns_.erase(columns_.begin() + startRemove, columns_.end());

    syncColumnHeights();
    QTimer::singleShot(0, this, [this]() { syncColumnHeights(); });
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
