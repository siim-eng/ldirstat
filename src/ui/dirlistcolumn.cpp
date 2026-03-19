#include "dirlistcolumn.h"
#include "entrytooltip.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

namespace ldirstat {

namespace {

QString formatSize(uint64_t bytes) {
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    uint64_t kb = bytes / 1024;
    if (kb < 1024)
        return QString::number(kb) + " KB";
    uint64_t mb = bytes / (1024 * 1024);
    if (mb < 1024)
        return QString::number(mb) + " MB";
    uint64_t gb = bytes / (1024ULL * 1024 * 1024);
    return QString::number(gb) + " GB";
}

QString formatPercent(uint64_t entrySize, uint64_t rootSize) {
    if (rootSize == 0)
        return {};
    double pct = 100.0 * entrySize / rootSize;
    if (pct < 0.05)
        return {};
    if (pct < 10.0)
        return QString::number(pct, 'f', 1) + "%";
    return QString::number(static_cast<int>(std::round(pct))) + "%";
}

} // namespace

DirListColumn::SizeTier DirListColumn::sizeTierFor(uint64_t bytes) {
    if (bytes < 1024) return SizeTier::Bytes;
    if (bytes < 1024 * 1024) return SizeTier::KB;
    if (bytes < 1024ULL * 1024 * 1024) return SizeTier::MB;
    return SizeTier::GB;
}

DirListColumn::DirListColumn(const DirEntryStore& store, const NameStore& names,
                             EntryRef dirRef, uint64_t rootSize,
                             const ThemeColors& themeColors, int columnWidth,
                             QWidget* parent)
    : QWidget(parent)
    , store_(store)
    , names_(names)
    , dirRef_(dirRef)
    , rootSize_(rootSize)
    , themeColors_(themeColors) {
    setFixedWidth(columnWidth);
    setMouseTracking(true);

    scrollBar_ = new QScrollBar(Qt::Vertical, this);
    scrollBar_->setFocusPolicy(Qt::NoFocus);
    connect(scrollBar_, &QScrollBar::valueChanged, this, [this]() { update(); });

    tooltipTimer_ = new QTimer(this);
    tooltipTimer_->setSingleShot(true);
    tooltipTimer_->setInterval(500);
    connect(tooltipTimer_, &QTimer::timeout, this, [this]() {
        if (hoverRow_ >= 0 && hoverRow_ < static_cast<int>(children_.size())) {
            QString tip = entryTooltip(store_, names_, children_[hoverRow_].ref);
            QToolTip::showText(hoverGlobalPos_, tip, this);
        }
    });

    QFontMetrics fm(font());
    sizeFieldWidth_ = fm.horizontalAdvance("9999 MB");
    pctFieldWidth_ = fm.horizontalAdvance("100%");

    buildChildList();
    updateScrollBar();
}

void DirListColumn::buildChildList() {
    const DirEntry& dir = store_[dirRef_];

    footerDirs_ = dir.dirCount;
    footerFiles_ = dir.fileCount;
    footerBytes_ = dir.size;

    children_.reserve(dir.childCount);

    EntryRef childRef = dir.firstChild;
    while (childRef.valid()) {
        const DirEntry& child = store_[childRef];
        auto sv = names_.get(child.name);

        ChildEntry ce;
        ce.ref = childRef;
        ce.sizeStr = formatSize(child.size);
        ce.pctStr = formatPercent(child.size, rootSize_);
        ce.name = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
        ce.isDir = child.isDir();
        ce.sizeTier = sizeTierFor(child.size);

        children_.push_back(std::move(ce));
        childRef = child.nextSibling;
    }
}

void DirListColumn::rebuild(uint64_t rootSize) {
    rootSize_ = rootSize;
    children_.clear();
    selectedIndex_ = -1;
    hoverRow_ = -1;
    buildChildList();
    updateScrollBar();
    update();
}

EntryRef DirListColumn::selectedRef() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(children_.size()))
        return children_[selectedIndex_].ref;
    return kNoEntry;
}

EntryRef DirListColumn::refAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(children_.size()))
        return kNoEntry;
    return children_[row].ref;
}

bool DirListColumn::rowIsDir(int row) const {
    return row >= 0 && row < static_cast<int>(children_.size()) && children_[row].isDir;
}

void DirListColumn::setSelectedIndex(int row) {
    if (row < 0 || row >= static_cast<int>(children_.size())) {
        clearSelection();
        return;
    }

    selectedIndex_ = row;
    ensureRowVisible(row);
    update();
}

void DirListColumn::setSelectedRef(EntryRef ref) {
    for (int i = 0; i < static_cast<int>(children_.size()); ++i) {
        if (children_[i].ref.pageId == ref.pageId &&
            children_[i].ref.index == ref.index) {
            setSelectedIndex(i);
            return;
        }
    }

    clearSelection();
}

void DirListColumn::setKeyboardActive(bool active) {
    if (keyboardActive_ == active)
        return;
    keyboardActive_ = active;
    update();
}

void DirListColumn::setThemeColors(const ThemeColors& colors) {
    themeColors_ = colors;
    update();
}

void DirListColumn::clearSelection() {
    if (selectedIndex_ < 0)
        return;
    selectedIndex_ = -1;
    update();
}

void DirListColumn::ensureRowVisible(int row) {
    if (row < 0 || row >= static_cast<int>(children_.size()))
        return;

    int listHeight = height() - kFooterHeight - kFooterGap;
    int rowTop = row * kRowHeight;
    int rowBottom = rowTop + kRowHeight;
    int scrollVal = scrollBar_->value();

    if (rowTop < scrollVal)
        scrollBar_->setValue(rowTop);
    else if (rowBottom > scrollVal + listHeight)
        scrollBar_->setValue(rowBottom - listHeight);
}

void DirListColumn::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette& pal = palette();

    int w = width();
    int scrollBarWidth = scrollBar_->isVisible() ? scrollBar_->width() : 0;
    int contentWidth = w - scrollBarWidth;
    int listHeight = height() - kFooterHeight - kFooterGap;
    int scrollOffset = scrollBar_->value();

    QFontMetrics fm = p.fontMetrics();

    // Background.
    p.fillRect(rect(), pal.base());

    if (keyboardActive_) {
        QColor focusColor = themeColors_.primaryForeground;
        focusColor.setAlpha(170);
        p.setPen(QPen(focusColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }

    if (!children_.empty()) {
        // Clip list area so rows don't bleed into footer.
        p.save();
        p.setClipRect(0, 0, contentWidth, listHeight);

        int firstRow = scrollOffset / kRowHeight;
        int lastRow = std::min(static_cast<int>(children_.size()) - 1,
                               (scrollOffset + listHeight) / kRowHeight);

        int arrowSpace = kArrowSize * 2 + kPadding;

        for (int i = firstRow; i <= lastRow; ++i) {
            const ChildEntry& ce = children_[i];
            int rowY = i * kRowHeight - scrollOffset;

            bool selected = (i == selectedIndex_);
            QColor textColor = selected ? pal.color(QPalette::HighlightedText)
                                        : pal.color(QPalette::Text);
            QColor sizeClr = textColor;
            if (!selected) {
                if (ce.sizeTier == SizeTier::MB) sizeClr = themeColors_.primaryForeground;
                else if (ce.sizeTier == SizeTier::GB) sizeClr = themeColors_.secondaryForeground;
            }

            // Selection highlight.
            if (selected)
                p.fillRect(0, rowY, contentWidth, kRowHeight, pal.highlight());

            int x = kLeftPadding;

            // Size (right-aligned, colored).
            p.setPen(sizeClr);
            p.drawText(x, rowY, sizeFieldWidth_, kRowHeight,
                       Qt::AlignRight | Qt::AlignVCenter, ce.sizeStr);
            x += sizeFieldWidth_ + kPadding;

            // Percentage (right-aligned, same color as size).
            p.drawText(x, rowY, pctFieldWidth_, kRowHeight,
                       Qt::AlignRight | Qt::AlignVCenter, ce.pctStr);
            x += pctFieldWidth_ + kPadding;

            // Name (default text color).
            p.setPen(textColor);
            int nameAvail = std::max(0, contentWidth - x - (ce.isDir ? arrowSpace : 0) - kPadding);
            QString elidedName = fm.elidedText(ce.name, Qt::ElideRight, nameAvail);
            p.drawText(x, rowY, nameAvail, kRowHeight,
                       Qt::AlignLeft | Qt::AlignVCenter, elidedName);

            // Directory indicator: filled triangle (default text color).
            if (ce.isDir) {
                int arrowX = contentWidth - kArrowSize - kPadding;
                int arrowY = rowY + (kRowHeight - kArrowSize * 2) / 2;
                QPolygon triangle;
                triangle << QPoint(arrowX, arrowY)
                         << QPoint(arrowX + kArrowSize, arrowY + kArrowSize)
                         << QPoint(arrowX, arrowY + kArrowSize * 2);
                p.setBrush(textColor);
                p.setPen(Qt::NoPen);
                p.drawPolygon(triangle);
                p.setBrush(Qt::NoBrush);
            }
        }

        p.restore();
    }
    // Footer separator.
    int footerY = height() - kFooterHeight;
    p.setPen(pal.mid().color());
    p.drawLine(0, footerY, contentWidth, footerY);

    // Footer text.
    p.setPen(pal.text().color());
    QString footerText = QString("%1 dirs, %2 files, %3")
        .arg(footerDirs_).arg(footerFiles_).arg(formatSize(footerBytes_));
    p.drawText(kLeftPadding, footerY, contentWidth - kLeftPadding - kPadding, kFooterHeight,
               Qt::AlignLeft | Qt::AlignVCenter, footerText);

    // Right border.
    p.setPen(pal.mid().color());
    p.drawLine(w - 1, 0, w - 1, height());
}

void DirListColumn::mousePressEvent(QMouseEvent* event) {
    int row = hitTestRow(event->pos());
    if (row < 0 || row >= static_cast<int>(children_.size()))
        return;

    selectedIndex_ = row;
    update();

    if (event->button() == Qt::RightButton)
        emit contextMenuRequested(children_[row].ref, event->globalPosition().toPoint());
    else
        emit entryClicked(children_[row].ref, children_[row].isDir);
}

void DirListColumn::mouseMoveEvent(QMouseEvent* event) {
    int row = hitTestRow(event->pos());
    if (row != hoverRow_) {
        QToolTip::hideText();
        hoverRow_ = row;
        if (row >= 0 && row < static_cast<int>(children_.size())) {
            hoverGlobalPos_ = event->globalPosition().toPoint();
            tooltipTimer_->start();
        } else {
            tooltipTimer_->stop();
        }
    } else {
        hoverGlobalPos_ = event->globalPosition().toPoint();
    }
}

void DirListColumn::wheelEvent(QWheelEvent* event) {
    int delta = -event->angleDelta().y();
    scrollBar_->setValue(scrollBar_->value() + delta);
}

void DirListColumn::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateScrollBar();
}

int DirListColumn::hitTestRow(const QPoint& pos) const {
    int listHeight = height() - kFooterHeight - kFooterGap;
    if (pos.y() >= listHeight)
        return -1;
    return (pos.y() + scrollBar_->value()) / kRowHeight;
}

void DirListColumn::updateScrollBar() {
    int listHeight = height() - kFooterHeight - kFooterGap;
    int contentHeight = static_cast<int>(children_.size()) * kRowHeight;

    if (contentHeight <= listHeight) {
        scrollBar_->setVisible(false);
        scrollBar_->setValue(0);
    } else {
        scrollBar_->setVisible(true);
        scrollBar_->setRange(0, contentHeight - listHeight);
        scrollBar_->setPageStep(listHeight);
        scrollBar_->setSingleStep(kRowHeight);
    }

    // Position scrollbar.
    scrollBar_->setGeometry(width() - scrollBar_->sizeHint().width(), 0,
                            scrollBar_->sizeHint().width(),
                            height() - kFooterHeight - kFooterGap);
}

} // namespace ldirstat
