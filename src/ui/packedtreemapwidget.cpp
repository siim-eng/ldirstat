#include "packedtreemapwidget.h"
#include "entrytooltip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QToolTip>

namespace ldirstat {

namespace {

constexpr qreal kSelectionBorderWidth = 3.0;
constexpr qreal kSelectionBorderInset = kSelectionBorderWidth * 0.5;
constexpr qreal kMinLabelWidth = 40.0;
constexpr qreal kMinLabelHeight = 16.0;
constexpr qreal kLabelMargin = 3.0;

QColor colorForEntry(const DirEntry &entry, const ThemeColors &colors) {
    if (entry.isDir()) return colors.primaryBackground;
    if (entry.isFile()) return colors.colorForFileCategory(FileCategorizer::categoryForType(entry.fileType));
    return colors.secondaryBackground;
}

QColor textColorForBackground(const QColor &background, const QPalette &palette) {
    if (background.lightness() < palette.color(QPalette::Window).lightness()) return palette.color(QPalette::BrightText);
    return palette.color(QPalette::Text);
}

QRectF toQRectF(const TreeMapRect &rect) {
    return {rect.x, rect.y, rect.width, rect.height};
}

bool drawable(const TreeMapRect &rect) {
    return rect.width >= 1.0f && rect.height >= 1.0f;
}

QString displayName(const NameStore &names, const DirEntry &entry) {
    const std::string_view name = names.get(entry.name);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

PackedTreeMapWidget::PackedTreeMapWidget(QWidget *parent)
    : GraphWidget(parent) {
    setMouseTracking(true);
    resizeDebounceTimer_ = new QTimer(this);
    resizeDebounceTimer_->setSingleShot(true);
    connect(resizeDebounceTimer_, &QTimer::timeout, this, [this]() {
        if (graphRect().size() != lastLayoutSize_) update();
    });
}

void PackedTreeMapWidget::setStores(const DirEntryStore *store, const NameStore *names) {
    store_ = store;
    names_ = names;
    dataChanged_ = true;
    // The old store may already be cleared.
    if (!store_) treeMap_ = PackedTreeMap();
    update();
}

void PackedTreeMapWidget::setDirectory(EntryRef dir) {
    currentDir_ = dir;
    dataChanged_ = true;
    update();
}

QRect PackedTreeMapWidget::graphRect() const {
    return rect().adjusted(kGraphInset, kGraphInset, -kGraphInset, -kGraphInset);
}

void PackedTreeMapWidget::ensureLayout() {
    const QSize size = graphRect().size();
    const bool sizeChanged = size != lastLayoutSize_;
    if (!dataChanged_ && !sizeChanged) return;
    // Only a pure resize may keep painting the previous layout.
    if (!dataChanged_ && resizeDebounceTimer_->isActive()) return;

    rebuildLayout();
    lastLayoutSize_ = size;
    dataChanged_ = false;
}

void PackedTreeMapWidget::rebuildLayout() {
    if (!store_ || !currentDir_.valid()) {
        treeMap_ = PackedTreeMap();
        return;
    }

    const QRect area = graphRect();
    PackedTreeMapOptions options;
    options.width = static_cast<float>(area.width());
    options.height = static_cast<float>(area.height());
    options.minNodeArea = 16.0f;
    options.minNodeSide = 3.0f;
    options.minDirAreaForChildren = 96.0f;
    options.minDirSideForChildren = 10.0f;
    options.maxDepth = 64;
    treeMap_.build(*store_, currentDir_, options);
}

void PackedTreeMapWidget::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    painter.fillRect(rect(), widgetPalette.color(QPalette::Window));

    const QRect area = graphRect();
    if (!store_ || !names_ || !currentDir_.valid() || area.width() <= 0 || area.height() <= 0) return;

    ensureLayout();
    if (treeMap_.nodes().empty()) return;

    painter.save();
    painter.translate(area.topLeft());
    painter.setClipRect(QRect(QPoint(0, 0), area.size()));
    paintTreeMap(painter, widgetPalette);
    painter.restore();
}

void PackedTreeMapWidget::paintTreeMap(QPainter &painter, const QPalette &widgetPalette) const {
    const std::vector<PackedTreeMapNode> &nodes = treeMap_.nodes();

    for (const PackedTreeMapNode &node : nodes)
        painter.fillRect(toQRectF(node.rect), colorForEntry((*store_)[node.ref], themeColors_));

    painter.setPen(widgetPalette.color(QPalette::Text));
    for (const PackedTreeMapNode &node : nodes) {
        if (drawable(node.rect)) painter.drawRect(toQRectF(node.rect));
    }

    const QFontMetrics metrics(font());
    for (const PackedTreeMapNode &node : nodes) {
        const DirEntry &entry = (*store_)[node.ref];
        const QRectF labelRect = toQRectF(entry.isDir() ? node.labelRect : node.rect);
        if (labelRect.width() < kMinLabelWidth || labelRect.height() < kMinLabelHeight) continue;

        const QRectF textRect = labelRect.adjusted(kLabelMargin, 0.0, -kLabelMargin, 0.0);
        const QString text = metrics.elidedText(displayName(*names_, entry), Qt::ElideRight, static_cast<int>(textRect.width()));
        if (text.isEmpty()) continue;

        painter.setPen(textColorForBackground(colorForEntry(entry, themeColors_), widgetPalette));
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    }

    if (!selectedEntry_.valid()) return;

    QPen selectionPen(themeColors_.selectionBorder, kSelectionBorderWidth);
    selectionPen.setCapStyle(Qt::SquareCap);
    selectionPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(selectionPen);
    painter.setBrush(Qt::NoBrush);

    for (const PackedTreeMapNode &node : nodes) {
        if (node.ref != selectedEntry_ || !drawable(node.rect)) continue;
        const QRectF outline = toQRectF(node.rect).adjusted(kSelectionBorderInset,
                                                            kSelectionBorderInset,
                                                            -kSelectionBorderInset,
                                                            -kSelectionBorderInset);
        if (outline.width() > 0.0 && outline.height() > 0.0) painter.drawRect(outline);
    }
}

void PackedTreeMapWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    resizeDebounceTimer_->start(kResizeDebounceMs);
    update();
}

void PackedTreeMapWidget::mousePressEvent(QMouseEvent *event) {
    const EntryRef ref = hitTest(event->pos());
    if (!ref.valid()) return;

    if (event->button() == Qt::RightButton)
        emit contextMenuRequested(ref, event->globalPosition().toPoint());
    else
        emit entrySelected(ref);
}

void PackedTreeMapWidget::mouseMoveEvent(QMouseEvent *event) {
    const EntryRef ref = hitTest(event->pos());
    if (ref.valid())
        QToolTip::showText(event->globalPosition().toPoint(), entryTooltip(*store_, *names_, ref));
    else
        QToolTip::hideText();
}

EntryRef PackedTreeMapWidget::hitTest(const QPoint &pos) {
    if (!store_ || !names_ || !currentDir_.valid()) return kNoEntry;

    const QRect area = graphRect();
    if (area.width() <= 0 || area.height() <= 0 || !area.contains(pos)) return kNoEntry;
    if (resizeDebounceTimer_->isActive() && area.size() != lastLayoutSize_) return kNoEntry;

    ensureLayout();
    return treeMap_.lookup(static_cast<float>(pos.x() - area.left()), static_cast<float>(pos.y() - area.top()));
}

} // namespace ldirstat
