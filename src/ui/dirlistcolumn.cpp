#include "dirlistcolumn.h"
#include "entrytooltip.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

namespace ldirstat {

namespace {

using SizeTier = DirListColumn::SizeTier;

SizeTier sizeTierFor(uint64_t bytes) {
    if (bytes < 1024) return SizeTier::Bytes;
    if (bytes < 1024 * 1024) return SizeTier::KB;
    if (bytes < 1024ULL * 1024 * 1024) return SizeTier::MB;
    return SizeTier::GB;
}

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

bool isDarkTheme(const QPalette& pal) {
    return pal.color(QPalette::Window).lightness() < 128;
}

QColor sizeColor(SizeTier tier, const QPalette& pal, bool selected) {
    if (selected)
        return pal.color(QPalette::HighlightedText);
    bool dark = isDarkTheme(pal);
    switch (tier) {
    case SizeTier::MB: return dark ? QColor(0x8F, 0xB3, 0xFF) : QColor(0x3E, 0x6F, 0xD1);
    case SizeTier::GB: return dark ? QColor(0xF0, 0xC6, 0x74) : QColor(0xA0, 0x6A, 0x00);
    default:           return pal.color(QPalette::Text);
    }
}

} // namespace

DirListColumn::DirListColumn(const DirEntryStore& store, const NameStore& names,
                             EntryRef dirRef, uint64_t rootSize, int columnWidth,
                             QWidget* parent)
    : QWidget(parent)
    , store_(store)
    , names_(names)
    , dirRef_(dirRef)
    , rootSize_(rootSize) {
    setFixedWidth(columnWidth);
    setMouseTracking(true);

    scrollBar_ = new QScrollBar(Qt::Vertical, this);
    connect(scrollBar_, &QScrollBar::valueChanged, this, [this]() { update(); });

    QFontMetrics fm(font());
    sizeFieldWidth_ = fm.horizontalAdvance("9999 MB");
    pctFieldWidth_ = fm.horizontalAdvance("100%");
    dirMarkerWidth_ = fm.horizontalAdvance(" >");

    buildChildList();
    updateScrollBar();
}

void DirListColumn::buildChildList() {
    const DirEntry& dir = store_[dirRef_];

    footerDirs_ = dir.dirCount;
    footerFiles_ = dir.fileCount;
    footerBytes_ = dir.size;

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

EntryRef DirListColumn::selectedRef() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(children_.size()))
        return children_[selectedIndex_].ref;
    return kNoEntry;
}

void DirListColumn::setSelectedRef(EntryRef ref) {
    for (int i = 0; i < static_cast<int>(children_.size()); ++i) {
        if (children_[i].ref.pageId == ref.pageId &&
            children_[i].ref.index == ref.index) {
            selectedIndex_ = i;

            // Scroll to make selected row visible.
            int listHeight = height() - kFooterHeight - kFooterGap;
            int rowTop = i * kRowHeight;
            int rowBottom = rowTop + kRowHeight;
            int scrollVal = scrollBar_->value();

            if (rowTop < scrollVal)
                scrollBar_->setValue(rowTop);
            else if (rowBottom > scrollVal + listHeight)
                scrollBar_->setValue(rowBottom - listHeight);

            update();
            return;
        }
    }
}

void DirListColumn::clearSelection() {
    selectedIndex_ = -1;
    update();
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

    // Clip list area so rows don't bleed into footer.
    p.save();
    p.setClipRect(0, 0, contentWidth, listHeight);

    // Draw list rows.
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
        QColor sizeClr = sizeColor(ce.sizeTier, pal, selected);

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
        int nameAvail = contentWidth - x - (ce.isDir ? arrowSpace : 0) - kPadding;
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
    if (row >= 0 && row < static_cast<int>(children_.size())) {
        selectedIndex_ = row;
        update();
        emit entryClicked(children_[row].ref, children_[row].isDir);
    }
}

void DirListColumn::mouseMoveEvent(QMouseEvent* event) {
    int row = hitTestRow(event->pos());
    if (row >= 0 && row < static_cast<int>(children_.size())) {
        QString tip = entryTooltip(store_, names_, children_[row].ref);
        QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    } else {
        QToolTip::hideText();
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
