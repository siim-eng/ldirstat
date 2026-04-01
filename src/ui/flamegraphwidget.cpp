#include "flamegraphwidget.h"
#include "entrytooltip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ldirstat {

namespace {

constexpr qreal kSelectionBorderWidth = 2.0;

struct GridPoint {
    int x;
    int y;

    bool operator==(const GridPoint &other) const = default;
};

struct DirectedEdge {
    GridPoint start;
    GridPoint end;
};

struct GridPointHash {
    size_t operator()(const GridPoint &point) const {
        return (static_cast<size_t>(static_cast<uint32_t>(point.x)) << 32) ^ static_cast<size_t>(static_cast<uint32_t>(point.y));
    }
};

QColor colorForEntry(const DirEntry &entry, const ThemeColors &colors) {
    if (entry.isDir()) return colors.primaryBackground;
    if (entry.isFile()) return colors.colorForFileCategory(FileCategorizer::categoryForType(entry.fileType));
    return colors.secondaryBackground;
}

QColor textColorForBackground(const QColor &background, const QPalette &palette) {
    const int backgroundLightness = background.lightness();
    const int windowLightness = palette.color(QPalette::Window).lightness();
    if (backgroundLightness < windowLightness) return palette.color(QPalette::BrightText);
    return palette.color(QPalette::Text);
}

int indexForCoord(const std::vector<int> &coords, int value) {
    auto it = std::lower_bound(coords.begin(), coords.end(), value);
    return static_cast<int>(std::distance(coords.begin(), it));
}

bool isCollinear(const GridPoint &a, const GridPoint &b, const GridPoint &c) {
    return (a.x == b.x && b.x == c.x) || (a.y == b.y && b.y == c.y);
}

std::vector<GridPoint> simplifyLoop(std::vector<GridPoint> points) {
    if (points.size() >= 2 && points.front() == points.back()) points.pop_back();

    bool changed = true;
    while (changed && points.size() >= 3) {
        changed = false;
        for (size_t i = 0; i < points.size() && points.size() >= 3;) {
            const size_t prev = (i + points.size() - 1) % points.size();
            const size_t next = (i + 1) % points.size();

            if (points[i] == points[prev] || points[i] == points[next] || isCollinear(points[prev], points[i], points[next])) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                continue;
            }

            ++i;
        }
    }

    return points;
}

} // namespace

FlameGraphWidget::FlameGraphWidget(QWidget *parent)
    : GraphWidget(parent) {
    setMouseTracking(true);
}

void FlameGraphWidget::setStores(const DirEntryStore *store, const NameStore *names) {
    store_ = store;
    names_ = names;
    layoutDirty_ = true;
    selectionContourDirty_ = true;
    update();
}

void FlameGraphWidget::setDirectory(EntryRef dir) {
    currentDir_ = dir;
    layoutDirty_ = true;
    selectionContourDirty_ = true;
    update();
}

QRect FlameGraphWidget::graphRect() const {
    return rect().adjusted(kGraphInset, kGraphInset, -kGraphInset, -kGraphInset);
}

void FlameGraphWidget::ensureLayout() {
    const QRect area = graphRect();
    if (!layoutDirty_ && area.size() == lastLayoutSize_) return;

    rebuildLayout();
    lastLayoutSize_ = area.size();
    layoutDirty_ = false;
}

void FlameGraphWidget::rebuildLayout() {
    if (!store_ || !currentDir_.valid()) {
        flameGraph_ = FlameGraph();
        return;
    }

    FlameGraphOptions options;
    options.width = static_cast<float>(graphRect().width());
    options.maxDepth = FlameGraph::kMaxDepthLimit;
    flameGraph_.build(*store_, currentDir_, options);
}

void FlameGraphWidget::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    const QColor backgroundColor = widgetPalette.color(QPalette::Window);
    const QColor borderColor = widgetPalette.color(QPalette::Text);
    painter.fillRect(rect(), backgroundColor);

    if (!store_ || !names_ || !currentDir_.valid()) return;

    const QRect graphArea = graphRect();
    if (graphArea.width() <= 0 || graphArea.height() <= 0) return;

    ensureLayout();
    if (flameGraph_.rowCount() == 0) return;

    painter.save();
    painter.setClipRect(graphArea);

    const int w = graphArea.width();
    const int xOffset = graphArea.left();
    const int yBase = graphArea.y() + graphArea.height();

    for (int r = 0; r < flameGraph_.rowCount(); ++r) {
        int y = yBase - (r + 1) * kRowHeight;
        if (y + kRowHeight <= graphArea.top()) break; // above visible area

        const auto &rects = flameGraph_.row(r);
        for (const auto &fr : rects) {
            int x1 = xOffset + static_cast<int>(fr.x1 * w);
            int x2 = xOffset + static_cast<int>(fr.x2 * w);

            const DirEntry &entry = (*store_)[fr.ref];
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
                painter.drawText(rect.adjusted(2, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
            }
        }
    }

    ensureSelectionContour();
    if (!selectionContourPath_.isEmpty()) {
        QPen selectionPen(themeColors_.selectionBorder, kSelectionBorderWidth);
        selectionPen.setCapStyle(Qt::SquareCap);
        selectionPen.setJoinStyle(Qt::MiterJoin);
        painter.strokePath(selectionContourPath_, selectionPen);
    }

    painter.restore();
}

void FlameGraphWidget::resizeEvent(QResizeEvent *event) {
    layoutDirty_ = true;
    selectionContourDirty_ = true;
    QWidget::resizeEvent(event);
}

void FlameGraphWidget::mousePressEvent(QMouseEvent *event) {
    EntryRef ref = hitTest(event->pos());
    if (!ref.valid()) return;

    if (event->button() == Qt::RightButton)
        emit contextMenuRequested(ref, event->globalPosition().toPoint());
    else
        emit entrySelected(ref);
}

void FlameGraphWidget::mouseMoveEvent(QMouseEvent *event) {
    EntryRef ref = hitTest(event->pos());
    if (ref.valid()) {
        QString tip = entryTooltip(*store_, *names_, ref);
        QToolTip::showText(event->globalPosition().toPoint(), tip);
    } else {
        QToolTip::hideText();
    }
}

EntryRef FlameGraphWidget::hitTest(const QPoint &pos) {
    if (!store_ || !currentDir_.valid()) return kNoEntry;

    const QRect graphArea = graphRect();
    if (!graphArea.contains(pos) || graphArea.width() <= 0) return kNoEntry;

    ensureLayout();
    if (flameGraph_.rowCount() == 0) return kNoEntry;

    int yBase = graphArea.y() + graphArea.height();
    int row = (yBase - 1 - pos.y()) / kRowHeight;
    if (row < 0) return kNoEntry;

    float relX = static_cast<float>(pos.x() - graphArea.left()) / graphArea.width();
    return flameGraph_.lookup(relX, row);
}

void FlameGraphWidget::ensureSelectionContour() {
    const QRect area = graphRect();
    if (!selectionContourDirty_ && cachedContourGraphRect_ == area && cachedContourEntry_ == selectedEntry_) {
        return;
    }

    rebuildSelectionContour();
}

void FlameGraphWidget::rebuildSelectionContour() {
    selectionContourPath_ = QPainterPath();
    cachedContourGraphRect_ = graphRect();
    cachedContourEntry_ = selectedEntry_;
    selectionContourDirty_ = false;

    if (!store_ || !selectedEntry_.valid()) return;
    if (cachedContourGraphRect_.width() <= 0 || cachedContourGraphRect_.height() <= 0) return;

    const std::vector<QRect> rects = collectSelectedSubtreeRects(cachedContourGraphRect_);
    if (rects.empty()) return;

    std::vector<int> xCoords;
    std::vector<int> yCoords;
    xCoords.reserve(rects.size() * 2);
    yCoords.reserve(rects.size() * 2);

    for (const QRect &rect : rects) {
        xCoords.push_back(rect.x());
        xCoords.push_back(rect.x() + rect.width());
        yCoords.push_back(rect.y());
        yCoords.push_back(rect.y() + rect.height());
    }

    std::sort(xCoords.begin(), xCoords.end());
    xCoords.erase(std::unique(xCoords.begin(), xCoords.end()), xCoords.end());
    std::sort(yCoords.begin(), yCoords.end());
    yCoords.erase(std::unique(yCoords.begin(), yCoords.end()), yCoords.end());

    if (xCoords.size() < 2 || yCoords.size() < 2) return;

    const int gridWidth = static_cast<int>(xCoords.size()) - 1;
    const int gridHeight = static_cast<int>(yCoords.size()) - 1;
    std::vector<uint8_t> occupied(static_cast<size_t>(gridWidth * gridHeight), 0);
    const auto cellIndex = [gridWidth](int x, int y) {
        return static_cast<size_t>(y) * static_cast<size_t>(gridWidth) + static_cast<size_t>(x);
    };

    for (const QRect &rect : rects) {
        const int x0 = indexForCoord(xCoords, rect.x());
        const int x1 = indexForCoord(xCoords, rect.x() + rect.width());
        const int y0 = indexForCoord(yCoords, rect.y());
        const int y1 = indexForCoord(yCoords, rect.y() + rect.height());

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x)
                occupied[cellIndex(x, y)] = 1;
        }
    }

    std::vector<DirectedEdge> edges;
    edges.reserve(rects.size() * 8);
    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            if (!occupied[cellIndex(x, y)]) continue;

            const GridPoint topLeft{xCoords[x], yCoords[y]};
            const GridPoint topRight{xCoords[x + 1], yCoords[y]};
            const GridPoint bottomRight{xCoords[x + 1], yCoords[y + 1]};
            const GridPoint bottomLeft{xCoords[x], yCoords[y + 1]};

            if (y == 0 || !occupied[cellIndex(x, y - 1)]) edges.push_back({topLeft, topRight});
            if (x == gridWidth - 1 || !occupied[cellIndex(x + 1, y)]) edges.push_back({topRight, bottomRight});
            if (y == gridHeight - 1 || !occupied[cellIndex(x, y + 1)]) edges.push_back({bottomRight, bottomLeft});
            if (x == 0 || !occupied[cellIndex(x - 1, y)]) edges.push_back({bottomLeft, topLeft});
        }
    }

    if (edges.empty()) return;

    std::unordered_multimap<GridPoint, int, GridPointHash> outgoingEdges;
    outgoingEdges.reserve(edges.size());
    for (int i = 0; i < static_cast<int>(edges.size()); ++i)
        outgoingEdges.emplace(edges[i].start, i);

    std::vector<uint8_t> visited(edges.size(), 0);
    for (int edgeIndex = 0; edgeIndex < static_cast<int>(edges.size()); ++edgeIndex) {
        if (visited[edgeIndex]) continue;

        std::vector<GridPoint> loop;
        int currentEdgeIndex = edgeIndex;
        const GridPoint loopStart = edges[currentEdgeIndex].start;

        while (true) {
            if (visited[currentEdgeIndex]) {
                loop.clear();
                break;
            }

            const DirectedEdge &edge = edges[currentEdgeIndex];
            visited[currentEdgeIndex] = 1;

            if (loop.empty()) loop.push_back(edge.start);
            loop.push_back(edge.end);

            if (edge.end == loopStart) break;

            auto range = outgoingEdges.equal_range(edge.end);
            currentEdgeIndex = -1;
            for (auto it = range.first; it != range.second; ++it) {
                if (!visited[it->second]) {
                    currentEdgeIndex = it->second;
                    break;
                }
            }

            if (currentEdgeIndex < 0) {
                loop.clear();
                break;
            }
        }

        loop = simplifyLoop(std::move(loop));
        if (loop.size() < 3) continue;

        selectionContourPath_.moveTo(loop.front().x, loop.front().y);
        for (size_t i = 1; i < loop.size(); ++i)
            selectionContourPath_.lineTo(loop[i].x, loop[i].y);
        selectionContourPath_.closeSubpath();
    }
}

std::vector<QRect> FlameGraphWidget::collectSelectedSubtreeRects(const QRect &graphArea) const {
    std::vector<QRect> rects;
    if (!store_ || !selectedEntry_.valid() || flameGraph_.rowCount() == 0) return rects;

    const int width = graphArea.width();
    const int xOffset = graphArea.left();
    const int yBase = graphArea.y() + graphArea.height();

    for (int r = 0; r < flameGraph_.rowCount(); ++r) {
        const int y = yBase - (r + 1) * kRowHeight;
        if (y + kRowHeight <= graphArea.top()) break;

        const auto &rectRow = flameGraph_.row(r);
        for (const FlameRect &fr : rectRow) {
            if (!isInSelectedSubtree(fr.ref)) continue;

            const int x1 = xOffset + static_cast<int>(fr.x1 * width);
            const int x2 = xOffset + static_cast<int>(fr.x2 * width);
            rects.emplace_back(x1, y, x2 - x1, kRowHeight);
        }
    }

    return rects;
}

bool FlameGraphWidget::isInSelectedSubtree(EntryRef ref) const {
    if (!store_ || !selectedEntry_.valid()) return false;

    while (ref.valid()) {
        if (ref == selectedEntry_) return true;
        ref = (*store_)[ref].parent;
    }

    return false;
}

} // namespace ldirstat
