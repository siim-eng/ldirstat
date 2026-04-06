#include "dirlistcolumn.h"
#include "filecategorystatsdialog.h"
#include "iconutil.h"
#include "modifiedtimehistogramdialog.h"

#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ldirstat {

namespace {

QString formatSize(uint64_t bytes) {
    auto formatUnit = [](double value, const char *suffix) {
        if (value < 10.0) {
            const double truncated = std::floor(value * 10.0) / 10.0;
            return QString::number(truncated, 'f', 1) + " " + suffix;
        }
        return QString::number(static_cast<uint64_t>(value)) + " " + suffix;
    };

    if (bytes < 1024) return QString::number(bytes) + " B";

    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return formatUnit(kb, "KB");

    const double mb = bytes / (1024.0 * 1024.0);
    if (mb < 1024.0) return formatUnit(mb, "MB");

    const double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return formatUnit(gb, "GB");
}

QString formatPercent(uint64_t entrySize, uint64_t rootSize) {
    if (rootSize == 0) return {};
    double pct = 100.0 * entrySize / rootSize;
    if (pct < 0.05) return {};
    if (pct < 10.0) return QString::number(pct, 'f', 1) + "%";
    return QString::number(static_cast<int>(std::round(pct))) + "%";
}

bool matchesFilter(std::string_view haystack, const QByteArray &needle) {
    if (needle.isEmpty()) return true;

    // TODO: Replace this raw UTF-8 byte search with StringZilla / faster UTF handling later.
    const std::string_view needleView(needle.constData(), static_cast<size_t>(needle.size()));
    return haystack.find(needleView) != std::string_view::npos;
}

QColor makePathHighlightColor(const QPalette &palette, const ThemeColors &themeColors) {
    QColor color = themeColors.primaryForeground;
    color.setAlpha(palette.color(QPalette::Window).lightness() < 128 ? 48 : 28);
    return color;
}

} // namespace

DirListColumn::SizeTier DirListColumn::sizeTierFor(uint64_t bytes) {
    if (bytes < 1024) return SizeTier::Bytes;
    if (bytes < 1024ULL * 1024) return SizeTier::KB;
    if (bytes < 1024ULL * 1024 * 1024) return SizeTier::MB;
    return SizeTier::GB;
}

DirListColumn::DirListColumn(const DirEntryStore &store,
                             const NameStore &names,
                             EntryRef dirRef,
                             uint64_t rootSize,
                             const ThemeColors &themeColors,
                             int columnWidth,
                             QWidget *parent)
    : QWidget(parent),
      store_(store),
      names_(names),
      dirRef_(dirRef),
      rootSize_(rootSize),
      themeColors_(themeColors),
      pathHighlightColor_(makePathHighlightColor(palette(), themeColors_)) {
    setFixedWidth(columnWidth);

    scrollBar_ = new QScrollBar(Qt::Vertical, this);
    scrollBar_->setFocusPolicy(Qt::NoFocus);
    connect(scrollBar_, &QScrollBar::valueChanged, this, [this]() { update(); });

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText(tr("Filter"));
    filterEdit_->installEventFilter(this);

    filterMenuButton_ = new QToolButton(this);
    filterMenuButton_->setFocusPolicy(Qt::NoFocus);
    filterMenuButton_->setPopupMode(QToolButton::InstantPopup);
    filterMenuButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    filterMenuButton_->setIcon(themedIcon(this, QStringLiteral("open-menu-symbolic"), QStyle::SP_ArrowDown));
    filterMenuButton_->setToolTip(tr("Filter/Selection Actions"));
    filterMenuButton_->installEventFilter(this);

    filterMenu_ = new QMenu(filterMenuButton_);
    filterMenuButton_->setMenu(filterMenu_);

    QAction *selectAllAction = filterMenu_->addAction(tr("Select All"));
    QAction *clearFilterAction = filterMenu_->addAction(tr("Clear Filter"));
    QAction *invertSelectionAction = filterMenu_->addAction(tr("Invert Selection"));
    filterMenu_->addSeparator();
    QAction *fileCategoryStatsAction = filterMenu_->addAction(tr("File Category Statistics..."));
    QAction *modifiedTimeHistogramAction = filterMenu_->addAction(tr("Modified Time Histogram..."));

    connect(selectAllAction, &QAction::triggered, this, [this]() {
        emit activated();
        selectAllVisible();
    });
    connect(clearFilterAction, &QAction::triggered, this, [this]() {
        emit activated();
        clearFilter();
    });
    connect(invertSelectionAction, &QAction::triggered, this, [this]() {
        emit activated();
        invertVisibleSelection();
    });
    connect(fileCategoryStatsAction, &QAction::triggered, this, [this]() {
        emit activated();
        showFileCategoryStatsDialog();
    });
    connect(modifiedTimeHistogramAction, &QAction::triggered, this, [this]() {
        emit activated();
        showModifiedTimeHistogramDialog();
    });

    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(250);
    connect(filterTimer_, &QTimer::timeout, this, &DirListColumn::applyFilter);
    connect(filterEdit_, &QLineEdit::textChanged, this, [this]() { filterTimer_->start(); });

    QFontMetrics fm(font());
    sizeFieldWidth_ = fm.horizontalAdvance("9999 MB");
    pctFieldWidth_ = fm.horizontalAdvance("100%");

    buildChildList();
    rebuildVisibleRows();
    updateScrollBar();
    layoutChildWidgets();
}

bool DirListColumn::eventFilter(QObject *watched, QEvent *event) {
    if (watched == filterEdit_ || watched == filterMenuButton_) {
        switch (event->type()) {
        case QEvent::FocusIn:
        case QEvent::MouseButtonPress: emit activated(); break;
        default: break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void DirListColumn::buildChildList() {
    const DirEntry &dir = store_[dirRef_];

    children_.clear();
    children_.reserve(dir.childCount);

    EntryRef childRef = dir.firstChild;
    while (childRef.valid()) {
        const DirEntry &child = store_[childRef];
        auto sv = names_.get(child.name);

        ChildEntry ce;
        ce.ref = childRef;
        ce.sizeStr = child.isMountPoint() ? tr("mnt") : formatSize(child.size);
        ce.pctStr = formatPercent(child.size, rootSize_);
        ce.name = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
        ce.isDir = child.isDir();
        ce.isMountPoint = child.isMountPoint();
        ce.sizeTier = sizeTierFor(child.size);

        children_.push_back(std::move(ce));
        childRef = child.nextSibling;
    }

    visibleRows_.clear();
    visibleRowByChild_.assign(children_.size(), -1);
    selectionFlags_.assign(children_.size(), 0);
    focusedChildIndex_ = -1;
    anchorChildIndex_ = -1;
    pathChildIndex_ = -1;
    selectedCount_ = 0;
}

void DirListColumn::rebuildVisibleRows() {
    visibleRows_.clear();
    visibleRows_.reserve(children_.size());
    std::fill(visibleRowByChild_.begin(), visibleRowByChild_.end(), -1);

    const bool filtering = !appliedFilterUtf8_.isEmpty();
    if (filtering) {
        footerDirs_ = 0;
        footerFiles_ = 0;
        footerBytes_ = 0;
    } else {
        const DirEntry &dir = store_[dirRef_];
        footerDirs_ = dir.dirCount;
        footerFiles_ = dir.fileCount;
        footerBytes_ = dir.size;
    }

    for (int childIndex = 0; childIndex < static_cast<int>(children_.size()); ++childIndex) {
        const EntryRef ref = children_[childIndex].ref;
        if (!matchesFilter(names_.get(store_[ref].name), appliedFilterUtf8_)) continue;

        visibleRowByChild_[childIndex] = static_cast<int>(visibleRows_.size());
        visibleRows_.push_back(childIndex);

        if (!filtering) continue;

        const DirEntry &child = store_[ref];
        if (child.isDir()) {
            footerDirs_ += child.dirCount + 1;
            footerFiles_ += child.fileCount;
        } else {
            ++footerFiles_;
        }
        footerBytes_ += child.size;
    }
}

void DirListColumn::applyFilter() {
    const EntryRef previousFocus = focusedRef();
    appliedFilterUtf8_ = filterEdit_->text().toUtf8();

    rebuildVisibleRows();

    for (int childIndex = 0; childIndex < static_cast<int>(children_.size()); ++childIndex) {
        if (selectionFlags_[childIndex] != 0 && visibleRowByChild_[childIndex] < 0) setSelectionState(childIndex, false);
    }

    if (focusedChildIndex_ >= 0 && visibleRowByChild_[focusedChildIndex_] < 0) focusedChildIndex_ = -1;

    if (focusedChildIndex_ < 0) {
        for (int childIndex : visibleRows_) {
            if (selectionFlags_[childIndex] != 0) {
                focusedChildIndex_ = childIndex;
                break;
            }
        }
    }

    if (focusedChildIndex_ < 0 && !visibleRows_.empty() && previousFocus.valid()) focusedChildIndex_ = visibleRows_.front();

    if (anchorChildIndex_ >= 0 && visibleRowByChild_[anchorChildIndex_] < 0) anchorChildIndex_ = focusedChildIndex_;
    if (pathChildIndex_ >= 0 && visibleRowByChild_[pathChildIndex_] < 0) pathChildIndex_ = -1;

    updateScrollBar();
    layoutChildWidgets();
    if (focusedIndex() >= 0) ensureRowVisible(focusedIndex());
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

void DirListColumn::showFileCategoryStatsDialog() {
    FileCategoryStatsDialog dialog(store_, names_, dirRef_, themeColors_, window());
    dialog.exec();
}

void DirListColumn::showModifiedTimeHistogramDialog() {
    ModifiedTimeHistogramDialog dialog(store_, names_, dirRef_, themeColors_, window());
    dialog.exec();
}

void DirListColumn::rebuild(uint64_t rootSize) {
    const EntryRef previousFocus = focusedRef();
    const std::vector<EntryRef> previousSelection = selectedRefs();
    const EntryRef previousPath = pathChildIndex_ >= 0 ? children_[pathChildIndex_].ref : kNoEntry;

    rootSize_ = rootSize;
    buildChildList();
    appliedFilterUtf8_ = filterEdit_->text().toUtf8();
    rebuildVisibleRows();

    for (EntryRef ref : previousSelection) {
        const int childIndex = childIndexForRef(ref);
        if (childIndex >= 0 && visibleRowByChild_[childIndex] >= 0) setSelectionState(childIndex, true);
    }

    const int focusChildIndex = childIndexForRef(previousFocus);
    if (focusChildIndex >= 0 && visibleRowByChild_[focusChildIndex] >= 0)
        focusedChildIndex_ = focusChildIndex;
    else if (!visibleRows_.empty() && previousFocus.valid())
        focusedChildIndex_ = visibleRows_.front();

    anchorChildIndex_ = focusedChildIndex_;
    setPathRef(previousPath);
    updateScrollBar();
    layoutChildWidgets();
    if (focusedIndex() >= 0) ensureRowVisible(focusedIndex());
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

int DirListColumn::focusedIndex() const {
    if (focusedChildIndex_ < 0 || focusedChildIndex_ >= static_cast<int>(visibleRowByChild_.size())) return -1;
    return visibleRowByChild_[focusedChildIndex_];
}

EntryRef DirListColumn::focusedRef() const {
    if (focusedChildIndex_ >= 0 && focusedChildIndex_ < static_cast<int>(children_.size()))
        return children_[focusedChildIndex_].ref;
    return kNoEntry;
}

std::vector<EntryRef> DirListColumn::selectedRefs() const {
    std::vector<EntryRef> refs;
    refs.reserve(selectedCount_);
    for (int childIndex = 0; childIndex < static_cast<int>(children_.size()); ++childIndex) {
        if (selectionFlags_[childIndex] != 0) refs.push_back(children_[childIndex].ref);
    }
    return refs;
}

EntryRef DirListColumn::refAtRow(int row) const {
    const int childIndex = childIndexAtRow(row);
    return childIndex >= 0 ? children_[childIndex].ref : kNoEntry;
}

bool DirListColumn::rowIsDir(int row) const {
    const int childIndex = childIndexAtRow(row);
    return childIndex >= 0 && children_[childIndex].isDir;
}

void DirListColumn::applySelectionAtRow(int row, Qt::KeyboardModifiers modifiers, bool preserveSelection) {
    applyMouseSelection(childIndexAtRow(row), modifiers, preserveSelection);
}

void DirListColumn::setFocusedIndex(int row, bool selectFocused) {
    const EntryRef previousFocus = focusedRef();
    const int childIndex = childIndexAtRow(row);
    if (childIndex < 0) {
        if (selectFocused) clearSelection();
        clearFocus();
        return;
    }

    focusedChildIndex_ = childIndex;
    anchorChildIndex_ = childIndex;
    if (selectFocused) setSingleSelection(childIndex);

    ensureRowVisible(row);
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

void DirListColumn::setFocusedRef(EntryRef ref, bool selectFocused) {
    const int childIndex = childIndexForRef(ref);
    if (childIndex < 0 || visibleRowByChild_[childIndex] < 0) {
        if (selectFocused) clearSelection();
        clearFocus();
        return;
    }

    setFocusedIndex(visibleRowByChild_[childIndex], selectFocused);
}

void DirListColumn::setPathRef(EntryRef ref) {
    const int previousPath = pathChildIndex_;
    const int childIndex = childIndexForRef(ref);
    pathChildIndex_ = (childIndex >= 0 && visibleRowByChild_[childIndex] >= 0) ? childIndex : -1;

    if (pathChildIndex_ != previousPath) update();
}

void DirListColumn::setKeyboardActive(bool active) {
    if (keyboardActive_ == active) return;
    keyboardActive_ = active;
    update();
}

void DirListColumn::setThemeColors(const ThemeColors &colors) {
    themeColors_ = colors;
    pathHighlightColor_ = makePathHighlightColor(palette(), themeColors_);
    update();
}

void DirListColumn::clearSelection() {
    if (selectedCount_ == 0) return;

    std::fill(selectionFlags_.begin(), selectionFlags_.end(), 0);
    selectedCount_ = 0;
    update();
}

void DirListColumn::clearFocus() {
    const EntryRef previousFocus = focusedRef();
    if (!previousFocus.valid()) return;

    focusedChildIndex_ = -1;
    anchorChildIndex_ = -1;
    update();
    emitFocusChanged();
}

void DirListColumn::selectAllVisible() {
    const EntryRef previousFocus = focusedRef();

    std::fill(selectionFlags_.begin(), selectionFlags_.end(), 0);
    selectedCount_ = 0;
    for (int childIndex : visibleRows_)
        setSelectionState(childIndex, true);

    if (focusedChildIndex_ < 0 && !visibleRows_.empty()) focusedChildIndex_ = visibleRows_.front();
    if (focusedChildIndex_ >= 0 && visibleRowByChild_[focusedChildIndex_] < 0)
        focusedChildIndex_ = visibleRows_.empty() ? -1 : visibleRows_.front();
    anchorChildIndex_ = focusedChildIndex_;

    if (focusedIndex() >= 0) ensureRowVisible(focusedIndex());
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

void DirListColumn::invertVisibleSelection() {
    const EntryRef previousFocus = focusedRef();

    for (int childIndex = 0; childIndex < static_cast<int>(children_.size()); ++childIndex) {
        if (visibleRowByChild_[childIndex] >= 0) {
            setSelectionState(childIndex, selectionFlags_[childIndex] == 0);
            continue;
        }

        if (selectionFlags_[childIndex] != 0) setSelectionState(childIndex, false);
    }

    if (focusedChildIndex_ < 0 && !visibleRows_.empty()) focusedChildIndex_ = visibleRows_.front();
    if (focusedChildIndex_ >= 0 && visibleRowByChild_[focusedChildIndex_] < 0)
        focusedChildIndex_ = visibleRows_.empty() ? -1 : visibleRows_.front();
    anchorChildIndex_ = focusedChildIndex_;

    if (focusedIndex() >= 0) ensureRowVisible(focusedIndex());
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

void DirListColumn::clearFilter() {
    if (filterEdit_->text().isEmpty()) return;

    filterTimer_->stop();
    filterEdit_->clear();
    applyFilter();
}

void DirListColumn::ensureRowVisible(int row) {
    if (row < 0 || row >= static_cast<int>(visibleRows_.size())) return;

    const int listHeight = listRect().height();
    const int rowTop = row * kRowHeight;
    const int rowBottom = rowTop + kRowHeight;
    const int scrollVal = scrollBar_->value();

    if (rowTop < scrollVal)
        scrollBar_->setValue(rowTop);
    else if (rowBottom > scrollVal + listHeight)
        scrollBar_->setValue(rowBottom - listHeight);
}

void DirListColumn::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), palette().base());

    if (keyboardActive_) {
        QColor focusColor = themeColors_.primaryForeground;
        focusColor.setAlpha(170);
        painter.setPen(QPen(focusColor, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }

    paintRows(painter, listRect());
    paintFooter(painter, footerRect());

    painter.setPen(palette().mid().color());
    painter.drawLine(width() - 1, 0, width() - 1, height());
}

void DirListColumn::mousePressEvent(QMouseEvent *event) {
    emit activated();

    const int row = hitTestRow(event->pos());
    const int childIndex = childIndexAtRow(row);
    if (childIndex < 0) return;

    if (event->button() == Qt::RightButton) {
        if (selectionFlags_[childIndex] == 0) {
            setSelectionState(childIndex, true);
            update();
        }
        emit contextMenuRequested(children_[childIndex].ref, event->globalPosition().toPoint());
        return;
    }

    const EntryRef previousFocus = focusedRef();
    const bool keepCurrentSelection = selectionFlags_[childIndex] != 0;
    applyMouseSelection(childIndex, event->modifiers(), keepCurrentSelection);

    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && focusedRef() == previousFocus
        && focusedChildIndex_ == childIndex && previousFocus.valid()) {
        emitFocusChanged();
    }
}

void DirListColumn::wheelEvent(QWheelEvent *event) {
    if (!scrollBar_->isVisible() || scrollBar_->maximum() <= 0) {
        event->accept();
        return;
    }

    if (!event->pixelDelta().isNull())
        scrollBar_->setValue(scrollBar_->value() - event->pixelDelta().y());
    else
        scrollBar_->setValue(scrollBar_->value() - event->angleDelta().y());

    event->accept();
}

void DirListColumn::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateScrollBar();
    layoutChildWidgets();
}

int DirListColumn::childIndexAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(visibleRows_.size())) return -1;
    return visibleRows_[row];
}

int DirListColumn::childIndexForRef(EntryRef ref) const {
    for (int childIndex = 0; childIndex < static_cast<int>(children_.size()); ++childIndex) {
        if (children_[childIndex].ref == ref) return childIndex;
    }
    return -1;
}

int DirListColumn::hitTestRow(const QPoint &pos) const {
    const QRect rowsRect = listRect();
    if (!rowsRect.contains(pos)) return -1;
    return (pos.y() + scrollBar_->value() - rowsRect.y()) / kRowHeight;
}

void DirListColumn::layoutChildWidgets() {
    const int scrollBarWidth = scrollBar_->isVisible() ? scrollBar_->sizeHint().width() : 0;
    const int contentWidth = width() - scrollBarWidth;
    const int filterHeight = filterEdit_->sizeHint().height();
    const int buttonWidth = filterMenuButton_->sizeHint().width();
    const int filterY = height() - kFilterBottomPadding - filterHeight;
    const int buttonX = std::max(kLeftPadding, contentWidth - kPadding - buttonWidth);
    const int editWidth = std::max(0, buttonX - kFilterButtonGap - kLeftPadding);

    filterEdit_->setGeometry(kLeftPadding, filterY, editWidth, filterHeight);
    filterMenuButton_->setGeometry(buttonX, filterY, buttonWidth, filterHeight);
}

void DirListColumn::applyMouseSelection(int childIndex, Qt::KeyboardModifiers modifiers, bool preserveSelection) {
    if (childIndex < 0 || visibleRowByChild_[childIndex] < 0) return;

    const EntryRef previousFocus = focusedRef();

    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);

    focusedChildIndex_ = childIndex;
    if (!shift) anchorChildIndex_ = childIndex;

    if (shift && anchorChildIndex_ >= 0 && visibleRowByChild_[anchorChildIndex_] >= 0) {
        selectVisibleRange(anchorChildIndex_, childIndex);
    } else if (ctrl) {
        if (!preserveSelection) setSelectionState(childIndex, selectionFlags_[childIndex] == 0);
    } else if (!preserveSelection || selectedCount_ != 1 || selectionFlags_[childIndex] == 0) {
        setSingleSelection(childIndex);
    }

    ensureRowVisible(visibleRowByChild_[childIndex]);
    update();

    if (focusedRef() != previousFocus) emitFocusChanged();
}

void DirListColumn::setSingleSelection(int childIndex) {
    std::fill(selectionFlags_.begin(), selectionFlags_.end(), 0);
    selectedCount_ = 0;

    if (childIndex >= 0) setSelectionState(childIndex, true);
}

void DirListColumn::setSelectionState(int childIndex, bool selected) {
    if (childIndex < 0 || childIndex >= static_cast<int>(selectionFlags_.size())) return;

    const uint8_t newValue = selected ? 1U : 0U;
    if (selectionFlags_[childIndex] == newValue) return;

    selectionFlags_[childIndex] = newValue;
    selectedCount_ += selected ? 1 : -1;
}

void DirListColumn::selectVisibleRange(int firstChildIndex, int lastChildIndex) {
    const int firstVisible = visibleRowByChild_[firstChildIndex];
    const int lastVisible = visibleRowByChild_[lastChildIndex];
    if (firstVisible < 0 || lastVisible < 0) {
        setSingleSelection(lastChildIndex);
        return;
    }

    std::fill(selectionFlags_.begin(), selectionFlags_.end(), 0);
    selectedCount_ = 0;

    const int begin = std::min(firstVisible, lastVisible);
    const int end = std::max(firstVisible, lastVisible);
    for (int row = begin; row <= end; ++row)
        setSelectionState(visibleRows_[row], true);
}

void DirListColumn::emitFocusChanged() {
    if (!focusedRef().valid()) {
        emit focusChanged(kNoEntry, false);
        return;
    }

    emit focusChanged(focusedRef(), children_[focusedChildIndex_].isDir);
}

QRect DirListColumn::listRect() const {
    const int scrollBarWidth = scrollBar_->isVisible() ? scrollBar_->width() : 0;
    const int contentWidth = width() - scrollBarWidth;
    const int filterHeight = filterEdit_->sizeHint().height();
    const int filterAreaHeight = filterHeight + kFilterGap + kFilterBottomPadding;
    const int listHeight = std::max(0, height() - filterAreaHeight - kFooterHeight - kFooterGap);
    return QRect(0, 0, contentWidth, listHeight);
}

QRect DirListColumn::footerRect() const {
    const QRect rowsRect = listRect();
    return QRect(0, rowsRect.height() + kFooterGap, rowsRect.width(), kFooterHeight);
}

void DirListColumn::updateScrollBar() {
    const QRect rowsRect = listRect();
    const int listHeight = rowsRect.height();
    const int contentHeight = static_cast<int>(visibleRows_.size()) * kRowHeight;

    if (contentHeight <= listHeight || listHeight <= 0) {
        scrollBar_->setVisible(false);
        scrollBar_->setValue(0);
    } else {
        scrollBar_->setVisible(true);
        scrollBar_->setRange(0, contentHeight - listHeight);
        scrollBar_->setPageStep(listHeight);
        scrollBar_->setSingleStep(kRowHeight);
    }

    scrollBar_->setGeometry(width() - scrollBar_->sizeHint().width(), 0, scrollBar_->sizeHint().width(), rowsRect.height());
}

void DirListColumn::paintRows(QPainter &painter, const QRect &rowsRect) {
    if (visibleRows_.empty() || rowsRect.height() <= 0) return;

    painter.save();
    painter.setClipRect(rowsRect);

    const QPalette &pal = palette();
    const QFontMetrics fm = painter.fontMetrics();
    const int scrollOffset = scrollBar_->value();
    const int firstRow = scrollOffset / kRowHeight;
    const int lastRow = std::min(static_cast<int>(visibleRows_.size()) - 1, (scrollOffset + rowsRect.height() - 1) / kRowHeight);
    const int arrowSpace = kArrowSize * 2 + kPadding;

    for (int row = firstRow; row <= lastRow; ++row) {
        const int childIndex = visibleRows_[row];
        const ChildEntry &child = children_[childIndex];
        const int rowY = rowsRect.y() + row * kRowHeight - scrollOffset;
        const bool selected = selectionFlags_[childIndex] != 0;
        const bool focused = childIndex == focusedChildIndex_;
        const bool pathRow = childIndex == pathChildIndex_ && childIndex != focusedChildIndex_;

        if (pathRow && !selected) painter.fillRect(0, rowY, rowsRect.width(), kRowHeight, pathHighlightColor_);
        if (selected) painter.fillRect(0, rowY, rowsRect.width(), kRowHeight, pal.highlight());

        QColor textColor = selected ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::Text);
        QColor sizeColor = textColor;
        if (!selected) {
            if (child.isMountPoint)
                sizeColor = themeColors_.mountForeground;
            else if (child.sizeTier == SizeTier::MB)
                sizeColor = themeColors_.primaryForeground;
            else if (child.sizeTier == SizeTier::GB)
                sizeColor = themeColors_.secondaryForeground;
        }

        int x = kLeftPadding;
        painter.setPen(sizeColor);
        painter.drawText(x, rowY, sizeFieldWidth_, kRowHeight, Qt::AlignRight | Qt::AlignVCenter, child.sizeStr);
        x += sizeFieldWidth_ + kPadding;

        painter.drawText(x, rowY, pctFieldWidth_, kRowHeight, Qt::AlignRight | Qt::AlignVCenter, child.pctStr);
        x += pctFieldWidth_ + kPadding;

        painter.setPen(textColor);
        const int nameAvail = std::max(0, rowsRect.width() - x - (child.isDir ? arrowSpace : 0) - kPadding);
        const QString elidedName = fm.elidedText(child.name, Qt::ElideRight, nameAvail);
        painter.drawText(x, rowY, nameAvail, kRowHeight, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

        if (child.isDir) {
            const int arrowX = rowsRect.width() - kArrowSize - kPadding;
            const int arrowY = rowY + (kRowHeight - kArrowSize * 2) / 2;
            QPolygon triangle;
            triangle << QPoint(arrowX, arrowY) << QPoint(arrowX + kArrowSize, arrowY + kArrowSize)
                     << QPoint(arrowX, arrowY + kArrowSize * 2);
            painter.setBrush(textColor);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
            painter.setBrush(Qt::NoBrush);
        }

        if (focused) {
            QColor outline = themeColors_.selectionBorder;
            outline.setAlpha(keyboardActive_ ? 220 : 170);
            painter.setPen(QPen(outline, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(0, rowY, rowsRect.width() - 1, kRowHeight - 1));
        }
    }

    painter.restore();
}

void DirListColumn::paintFooter(QPainter &painter, const QRect &footerRect) {
    const QPalette &pal = palette();

    painter.setPen(pal.mid().color());
    painter.drawLine(0, footerRect.y(), footerRect.width(), footerRect.y());

    painter.setPen(pal.text().color());
    const QString footerText = QString("%1 dirs, %2 files, %3").arg(footerDirs_).arg(footerFiles_).arg(formatSize(footerBytes_));
    painter.drawText(kLeftPadding,
                     footerRect.y(),
                     footerRect.width() - kLeftPadding - kPadding,
                     footerRect.height(),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     footerText);
}

} // namespace ldirstat
