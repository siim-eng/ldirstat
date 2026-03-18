#include "flamegraphwidget.h"
#include "entrytooltip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace ldirstat {

namespace {

QColor colorForEntry(const DirEntry& entry, const ThemeColors& colors) {
    if (entry.isDir())
        return colors.primaryBackground;
    return colors.secondaryBackground;
}

QColor textColorForBackground(const QColor& background, const QPalette& palette) {
    const int backgroundLightness = background.lightness();
    const int windowLightness = palette.color(QPalette::Window).lightness();
    if (backgroundLightness < windowLightness)
        return palette.color(QPalette::BrightText);
    return palette.color(QPalette::Text);
}

} // namespace

FlameGraphWidget::FlameGraphWidget(QWidget* parent)
    : GraphWidget(parent) {
    setMouseTracking(true);
}

void FlameGraphWidget::setStores(const DirEntryStore* store, const NameStore* names) {
    store_ = store;
    names_ = names;
}

void FlameGraphWidget::setDirectory(EntryRef dir) {
    if (!store_)
        return;
    flameGraph_.build(*store_, dir);
    update();
}

QRect FlameGraphWidget::graphRect() const {
    return rect().adjusted(kGraphInset, kGraphInset, -kGraphInset, -kGraphInset);
}

void FlameGraphWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    const QColor backgroundColor = widgetPalette.color(QPalette::Window);
    const QColor borderColor = widgetPalette.color(QPalette::Text);
    painter.fillRect(rect(), backgroundColor);

    if (!store_ || !names_ || flameGraph_.rowCount() == 0)
        return;

    const QRect graphArea = graphRect();
    if (graphArea.width() <= 0 || graphArea.height() <= 0)
        return;

    painter.save();
    painter.setClipRect(graphArea);

    const int w = graphArea.width();
    const int xOffset = graphArea.left();
    const int yBase = graphArea.y() + graphArea.height();

    for (int r = 0; r < flameGraph_.rowCount(); ++r) {
        int y = yBase - (r + 1) * kRowHeight;
        if (y + kRowHeight <= graphArea.top())
            break; // above visible area

        const auto& rects = flameGraph_.row(r);
        for (const auto& fr : rects) {
            int x1 = xOffset + static_cast<int>(fr.x1 * w);
            int x2 = xOffset + static_cast<int>(fr.x2 * w);
            if (x2 - x1 < 4)
                continue;

            const DirEntry& entry = (*store_)[fr.ref];
            QRect rect(x1, y, x2 - x1, kRowHeight);

            const QColor fillColor = colorForEntry(entry, themeColors_);
            painter.fillRect(rect, fillColor);
            painter.setPen(borderColor);
            painter.drawRect(rect);

            // Draw label if rect is wide enough.
            if (x2 - x1 > 40) {
                auto sv = names_->get(entry.name);
                auto name = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
                painter.setPen(textColorForBackground(fillColor, widgetPalette));
                painter.drawText(rect.adjusted(2, 0, -2, 0),
                                 Qt::AlignVCenter | Qt::AlignLeft, name);
            }
        }
    }
    painter.restore();
}

void FlameGraphWidget::mousePressEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid())
        emit entrySelected(ref);
}

void FlameGraphWidget::mouseMoveEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid()) {
        QString tip = entryTooltip(*store_, *names_, ref);
        QToolTip::showText(event->globalPosition().toPoint(), tip);
    } else {
        QToolTip::hideText();
    }
}

EntryRef FlameGraphWidget::hitTest(const QPoint& pos) const {
    if (flameGraph_.rowCount() == 0)
        return kNoEntry;

    const QRect graphArea = graphRect();
    if (!graphArea.contains(pos) || graphArea.width() <= 0)
        return kNoEntry;

    int yBase = graphArea.y() + graphArea.height();
    int row = (yBase - 1 - pos.y()) / kRowHeight;
    if (row < 0)
        return kNoEntry;

    float relX = static_cast<float>(pos.x() - graphArea.left()) / graphArea.width();
    return flameGraph_.lookup(relX, row);
}

} // namespace ldirstat
