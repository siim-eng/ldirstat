#include "flamegraphwidget.h"
#include "entrytooltip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace ldirstat {

namespace {

QColor colorForEntry(const DirEntry& entry) {
    if (entry.isDir())
        return QColor(70, 130, 180);   // steel blue
    return QColor(220, 120, 60);        // warm orange
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
    : QWidget(parent) {
    setMouseTracking(true);
}

void FlameGraphWidget::setGraph(const FlameGraph* graph, const DirEntryStore* store,
                                const NameStore* names) {
    graph_ = graph;
    store_ = store;
    names_ = names;
    update();
}

void FlameGraphWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    const QColor backgroundColor = widgetPalette.color(QPalette::Window);
    const QColor borderColor = widgetPalette.color(QPalette::Text);
    painter.fillRect(rect(), backgroundColor);

    if (!graph_ || !store_ || !names_ || graph_->rowCount() == 0)
        return;

    int w = width();
    int totalHeight = graph_->rowCount() * kRowHeight;
    int yBase = height(); // bottom of widget = row 0

    for (int r = 0; r < graph_->rowCount(); ++r) {
        int y = yBase - (r + 1) * kRowHeight;
        if (y + kRowHeight < 0)
            break; // above visible area

        const auto& rects = graph_->row(r);
        for (const auto& fr : rects) {
            int x1 = static_cast<int>(fr.x1 * w);
            int x2 = static_cast<int>(fr.x2 * w);
            if (x2 - x1 < 4)
                continue;

            const DirEntry& entry = (*store_)[fr.ref];
            QRect rect(x1, y, x2 - x1, kRowHeight);

            const QColor fillColor = colorForEntry(entry);
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
    Q_UNUSED(totalHeight);
}

void FlameGraphWidget::mousePressEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid())
        emit rectClicked(ref);
}

void FlameGraphWidget::mouseMoveEvent(QMouseEvent* event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid()) {
        QString tip = entryTooltip(*store_, *names_, ref);
        QToolTip::showText(event->globalPosition().toPoint(), tip);
        emit rectHovered(ref);
    } else {
        QToolTip::hideText();
    }
}

EntryRef FlameGraphWidget::hitTest(const QPoint& pos) const {
    if (!graph_ || graph_->rowCount() == 0)
        return kNoEntry;

    int yBase = height();
    int row = (yBase - pos.y()) / kRowHeight;
    if (row < 0)
        return kNoEntry;

    float relX = static_cast<float>(pos.x()) / width();
    return graph_->lookup(relX, row);
}

} // namespace ldirstat
