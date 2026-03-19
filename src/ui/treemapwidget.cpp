#include "treemapwidget.h"
#include "entrytooltip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>

namespace ldirstat {

namespace {

constexpr qreal kSelectionBorderWidth = 2.0;
constexpr qreal kSelectionBorderInset = kSelectionBorderWidth * 0.5;

QColor colorForEntry(const DirEntry& entry, const ThemeColors& colors) {
    return entry.isDir() ? colors.primaryBackground : colors.secondaryBackground;
}

QColor headerColorForFill(const QColor& fill) {
    if (fill.lightness() < 128)
        return fill.lighter(120);
    return fill.darker(110);
}

QColor textColorForBackground(const QColor& background, const QPalette& palette) {
    const int backgroundLightness = background.lightness();
    const int windowLightness = palette.color(QPalette::Window).lightness();
    if (backgroundLightness < windowLightness)
        return palette.color(QPalette::BrightText);
    return palette.color(QPalette::Text);
}

QRectF toQRect(const TreeMapRect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

QRectF selectionOutlineRect(const TreeMapRect& rect) {
    QRectF outline = toQRect(rect).adjusted(kSelectionBorderInset,
                                            kSelectionBorderInset,
                                            -kSelectionBorderInset,
                                            -kSelectionBorderInset);
    if (outline.width() <= 0.0 || outline.height() <= 0.0)
        return {};
    return outline;
}

QString entryName(const NameStore& names, const DirEntry& entry) {
    const std::string_view sv = names.get(entry.name);
    return QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
}

bool hasHeader(const TreeMapNode& node) {
    return node.contentRect.y > node.rect.y && node.contentRect.height < node.rect.height;
}

} // namespace

TreeMapWidget::TreeMapWidget(QWidget* parent)
    : GraphWidget(parent) {
    setMouseTracking(true);
}

void TreeMapWidget::setStores(const DirEntryStore* store, const NameStore* names) {
    store_ = store;
    names_ = names;
    layoutDirty_ = true;
    update();
}

void TreeMapWidget::setDirectory(EntryRef dir) {
    currentDir_ = dir;
    layoutDirty_ = true;
    update();
}

void TreeMapWidget::setRenderMode(RenderMode mode) {
    if (renderMode_ == mode)
        return;
    renderMode_ = mode;
    layoutDirty_ = true;
    update();
}

QRect TreeMapWidget::graphRect() const {
    return rect().adjusted(kGraphInset, kGraphInset, -kGraphInset, -kGraphInset);
}

void TreeMapWidget::ensureLayout() {
    const QRect area = graphRect();
    if (!layoutDirty_ && area.size() == lastLayoutSize_)
        return;

    rebuildLayout();
    lastLayoutSize_ = area.size();
    layoutDirty_ = false;
}

void TreeMapWidget::rebuildLayout() {
    if (!store_ || !currentDir_.valid()) {
        treeMap_ = TreeMap();
        return;
    }

    const QRect area = graphRect();
    TreeMapOptions options;
    options.width = static_cast<float>(area.width());
    options.height = static_cast<float>(area.height());
    options.minNodeArea = 4.0f;
    options.minDirAreaForChildren = 96.0f;
    options.minDirSideForChildren = 10.0f;
    options.maxDepth = 64;

    if (renderMode_ == RenderMode::DirectoryHeaders) {
        options.directoryHeaderHeight = static_cast<float>(fontMetrics().height() + 4);
        options.minDirAreaForChildren = std::max(options.minDirAreaForChildren,
                                                 options.directoryHeaderHeight * 18.0f);
        options.minDirSideForChildren =
            std::max(options.minDirSideForChildren, options.directoryHeaderHeight * 0.7f);
    }

    treeMap_.build(*store_, currentDir_, options);
}

void TreeMapWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    painter.fillRect(rect(), widgetPalette.color(QPalette::Window));

    if (!store_ || !names_ || !currentDir_.valid())
        return;

    const QRect area = graphRect();
    if (area.width() <= 0 || area.height() <= 0)
        return;

    ensureLayout();
    if (treeMap_.nodes().empty())
        return;

    painter.save();
    painter.translate(area.topLeft());
    painter.setClipRect(QRect(QPoint(0, 0), area.size()));

    if (renderMode_ == RenderMode::Packed)
        paintPacked(painter, widgetPalette);
    else
        paintDirectoryHeaders(painter, widgetPalette);

    painter.restore();
}

void TreeMapWidget::resizeEvent(QResizeEvent* event) {
    layoutDirty_ = true;
    QWidget::resizeEvent(event);
}

void TreeMapWidget::mousePressEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (!ref.valid())
        return;

    if (event->button() == Qt::RightButton)
        emit contextMenuRequested(ref, event->globalPosition().toPoint());
    else
        emit entrySelected(ref);
}

void TreeMapWidget::mouseMoveEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid()) {
        QString tip = entryTooltip(*store_, *names_, ref);
        QToolTip::showText(event->globalPosition().toPoint(), tip);
    } else {
        QToolTip::hideText();
    }
}

EntryRef TreeMapWidget::hitTest(const QPoint& pos) {
    if (!store_ || !currentDir_.valid())
        return kNoEntry;

    const QRect area = graphRect();
    if (!area.contains(pos) || area.width() <= 0 || area.height() <= 0)
        return kNoEntry;

    ensureLayout();
    if (treeMap_.nodes().empty())
        return kNoEntry;

    const float relX = static_cast<float>(pos.x() - area.left());
    const float relY = static_cast<float>(pos.y() - area.top());
    return treeMap_.lookup(relX, relY);
}

void TreeMapWidget::paintPacked(QPainter& painter, const QPalette& widgetPalette) const {
    const QColor borderColor = widgetPalette.color(QPalette::Text);
    const QFontMetrics metrics(font());

    for (const TreeMapNode& node : treeMap_.nodes()) {
        const DirEntry& entry = (*store_)[node.ref];
        painter.fillRect(toQRect(node.rect), colorForEntry(entry, themeColors_));
    }

    painter.setPen(borderColor);
    for (const TreeMapNode& node : treeMap_.nodes()) {
        if (node.rect.width < 1.0f || node.rect.height < 1.0f)
            continue;
        painter.drawRect(toQRect(node.rect));
    }

    for (const TreeMapNode& node : treeMap_.nodes()) {
        const DirEntry& entry = (*store_)[node.ref];
        const QRectF labelRect = entry.isDir() ? toQRect(node.labelRect) : toQRect(node.rect);

        if (labelRect.width() < 40.0f || labelRect.height() < 16.0f)
            continue;

        const QColor fill = colorForEntry(entry, themeColors_);
        const QRectF textRect = labelRect.adjusted(3.0, 0.0, -3.0, 0.0);
        const QString text = metrics.elidedText(entryName(*names_, entry),
                                                Qt::ElideRight,
                                                static_cast<int>(textRect.width()));
        if (text.isEmpty())
            continue;

        painter.setPen(textColorForBackground(fill, widgetPalette));
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    }

    if (selectedEntry_.valid()) {
        QPen selectionPen(themeColors_.selectionBorder, kSelectionBorderWidth);
        selectionPen.setCapStyle(Qt::SquareCap);
        selectionPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(selectionPen);
        painter.setBrush(Qt::NoBrush);

        for (const TreeMapNode& node : treeMap_.nodes()) {
            if (node.ref != selectedEntry_ || node.rect.width < 1.0f || node.rect.height < 1.0f)
                continue;
            const QRectF outline = selectionOutlineRect(node.rect);
            if (!outline.isValid() || outline.isEmpty())
                continue;
            painter.drawRect(outline);
        }
    }
}

void TreeMapWidget::paintDirectoryHeaders(QPainter& painter, const QPalette& widgetPalette) const {
    const QColor borderColor = widgetPalette.color(QPalette::Text);
    const QFontMetrics metrics(font());

    for (const TreeMapNode& node : treeMap_.nodes()) {
        const DirEntry& entry = (*store_)[node.ref];
        const QColor fill = colorForEntry(entry, themeColors_);
        painter.fillRect(toQRect(node.rect), fill);

        if (entry.isDir() && hasHeader(node)) {
            const QRectF headerRect(node.rect.x,
                                    node.rect.y,
                                    node.rect.width,
                                    node.contentRect.y - node.rect.y);
            painter.fillRect(headerRect, headerColorForFill(fill));
        }
    }

    painter.setPen(borderColor);
    for (const TreeMapNode& node : treeMap_.nodes()) {
        if (node.rect.width < 1.0f || node.rect.height < 1.0f)
            continue;

        painter.drawRect(toQRect(node.rect));

        if (hasHeader(node)) {
            painter.drawLine(QPointF(node.rect.x, node.contentRect.y),
                             QPointF(node.rect.right(), node.contentRect.y));
        }
    }

    for (const TreeMapNode& node : treeMap_.nodes()) {
        const DirEntry& entry = (*store_)[node.ref];
        const QColor fill = colorForEntry(entry, themeColors_);

        QRectF labelRect;
        if (entry.isDir() && hasHeader(node)) {
            labelRect = QRectF(node.rect.x,
                               node.rect.y,
                               node.rect.width,
                               node.contentRect.y - node.rect.y);
        } else {
            labelRect = toQRect(node.rect);
        }

        if (labelRect.width() < 40.0 || labelRect.height() < 16.0)
            continue;

        const QString text = metrics.elidedText(entryName(*names_, entry),
                                                Qt::ElideRight,
                                                static_cast<int>(labelRect.width()) - 6);
        if (text.isEmpty())
            continue;

        const QColor textColor = entry.isDir() && hasHeader(node)
            ? textColorForBackground(headerColorForFill(fill), widgetPalette)
            : textColorForBackground(fill, widgetPalette);
        painter.setPen(textColor);
        painter.drawText(labelRect.adjusted(3.0, 0.0, -3.0, 0.0),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         text);
    }

    if (selectedEntry_.valid()) {
        QPen selectionPen(themeColors_.selectionBorder, kSelectionBorderWidth);
        selectionPen.setCapStyle(Qt::SquareCap);
        selectionPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(selectionPen);
        painter.setBrush(Qt::NoBrush);

        for (const TreeMapNode& node : treeMap_.nodes()) {
            if (node.ref != selectedEntry_ || node.rect.width < 1.0f || node.rect.height < 1.0f)
                continue;
            const QRectF outline = selectionOutlineRect(node.rect);
            if (!outline.isValid() || outline.isEmpty())
                continue;
            painter.drawRect(outline);
        }
    }
}

} // namespace ldirstat
