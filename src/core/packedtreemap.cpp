#include "packedtreemap.h"

#include <algorithm>
#include <limits>

namespace ldirstat {

namespace {

constexpr size_t kMinReservedNodes = 4096;

EntryRef skipEmptySiblings(const DirEntryStore &store, EntryRef ref) {
    while (ref.valid() && layoutSizeOf(store[ref]) == 0)
        ref = store[ref].nextSibling;
    return ref;
}

uint64_t childrenLayoutSize(const DirEntryStore &store, const DirEntry &dir) {
    uint64_t total = 0;
    for (EntryRef ref = dir.firstChild; ref.valid(); ref = store[ref].nextSibling)
        total += layoutSizeOf(store[ref]);
    return total;
}

bool fitsChildren(const TreeMapRect &rect, const PackedTreeMapOptions &options) {
    return !rect.empty() && rect.area() >= options.minDirAreaForChildren
           && std::min(rect.width, rect.height) >= options.minDirSideForChildren;
}

TreeMapRect nonNegative(TreeMapRect rect) {
    rect.width = std::max(rect.width, 0.0f);
    rect.height = std::max(rect.height, 0.0f);
    return rect;
}

} // namespace

void PackedTreeMap::build(const DirEntryStore &store, EntryRef focus, const PackedTreeMapOptions &options) {
    nodes_.clear();
    if (!focus.valid() || options.width <= 0.0f || options.height <= 0.0f) return;

    const DirEntry &focusEntry = store[focus];
    const uint64_t focusSize = focusEntry.firstChild.valid() ? childrenLayoutSize(store, focusEntry) : layoutSizeOf(focusEntry);
    if (focusSize == 0) return;

    nodes_.reserve(std::max<size_t>(kMinReservedNodes, static_cast<size_t>(focusEntry.childCount) * 2 + 1));

    const TreeMapRect rootRect{0.0f, 0.0f, options.width, options.height};
    nodes_.push_back({rootRect, rootRect, focus, kNoNode, kNoNode, 0, false});

    // The growing node vector doubles as a breadth-first queue.
    for (uint32_t index = 0; index < nodes_.size(); ++index)
        layoutChildren(store, options, index);
}

void PackedTreeMap::layoutChildren(const DirEntryStore &store, const PackedTreeMapOptions &options, uint32_t nodeIndex) {
    const DirEntry &entry = store[nodes_[nodeIndex].ref];
    if (!entry.isDir() || !entry.firstChild.valid()) return;

    if (nodes_[nodeIndex].depth >= options.maxDepth || !fitsChildren(nodes_[nodeIndex].rect, options)) {
        nodes_[nodeIndex].childrenCulled = true;
        return;
    }

    uint64_t sizeLeft = childrenLayoutSize(store, entry);
    TreeMapRect freeRect = nodes_[nodeIndex].rect;
    uint32_t lastChild = kNoNode;
    EntryRef next = skipEmptySiblings(store, entry.firstChild);

    while (next.valid() && sizeLeft > 0) {
        if (freeRect.empty()) {
            nodes_[nodeIndex].childrenCulled = true;
            break;
        }

        // Recomputing the scale per row keeps float drift from accumulating.
        const double scale = static_cast<double>(freeRect.width) * freeRect.height / static_cast<double>(sizeLeft);
        const DirEntry &candidate = store[next];
        // Siblings are sorted by raw size, an upper bound for layoutSizeOf().
        if (static_cast<double>(candidate.size) * scale < options.minNodeArea) {
            nodes_[nodeIndex].childrenCulled = true;
            break;
        }
        // Hard-linked files can fall below the minimum out of order.
        if (static_cast<double>(layoutSizeOf(candidate)) * scale < options.minNodeArea) {
            nodes_[nodeIndex].childrenCulled = true;
            next = skipEmptySiblings(store, candidate.nextSibling);
            continue;
        }

        const double side = std::min(freeRect.width, freeRect.height);
        const Row row = gatherRow(store, options, next, side, scale);
        // Later rows hold smaller items, so a too-thin row ends the layout.
        const double thickness = row.area / side;
        if (thickness < options.minNodeSide || row.minArea / thickness < options.minNodeSide) {
            nodes_[nodeIndex].childrenCulled = true;
            break;
        }
        placeRow(store, row, scale, nodeIndex, freeRect, lastChild);
        sizeLeft -= row.size;
        next = skipEmptySiblings(store, row.end);
    }

    if (lastChild != kNoNode) nodes_[nodeIndex].labelRect = nonNegative(freeRect);
}

PackedTreeMap::Row PackedTreeMap::gatherRow(const DirEntryStore &store,
                                            const PackedTreeMapOptions &options,
                                            EntryRef first,
                                            double side,
                                            double scale) {
    Row row;
    row.first = first;
    row.end = first;
    double minArea = std::numeric_limits<double>::infinity();
    double maxArea = 0.0;

    while (row.end.valid()) {
        const DirEntry &candidate = store[row.end];
        const uint64_t size = layoutSizeOf(candidate);
        if (size > 0) {
            const double area = static_cast<double>(size) * scale;
            if (area < options.minNodeArea) break;

            const double trialArea = row.area + area;
            const double trialMin = std::min(minArea, area);
            const double trialMax = std::max(maxArea, area);
            if (row.count > 0
                && squarifyWorstAspect(trialArea, trialMin, trialMax, side)
                       > squarifyWorstAspect(row.area, minArea, maxArea, side)) {
                break;
            }

            row.area = trialArea;
            row.size += size;
            ++row.count;
            minArea = trialMin;
            maxArea = trialMax;
        }
        row.end = candidate.nextSibling;
    }

    row.minArea = minArea;
    return row;
}

void PackedTreeMap::placeRow(const DirEntryStore &store,
                             const Row &row,
                             double scale,
                             uint32_t parentIndex,
                             TreeMapRect &freeRect,
                             uint32_t &lastChild) {
    // Lay the row along the shorter side of the free rect, at its left (wide
    // rect) or top (tall rect) edge; the final item absorbs rounding error.
    const bool column = freeRect.width >= freeRect.height;
    const double thickness = row.area / (column ? freeRect.height : freeRect.width);
    const float sliceStart = column ? freeRect.x : freeRect.y;
    const double sliceEdge = sliceStart + thickness;
    // Round after subtracting to preserve the original packed geometry.
    const auto sliceThickness = static_cast<float>(sliceEdge - sliceStart);
    const double rowEnd = column ? freeRect.bottom() : freeRect.right();
    double cursor = column ? freeRect.y : freeRect.x;
    uint32_t placed = 0;

    for (EntryRef ref = row.first; ref != row.end; ref = store[ref].nextSibling) {
        const uint64_t size = layoutSizeOf(store[ref]);
        if (size == 0) continue;

        ++placed;
        const double length = placed == row.count ? rowEnd - cursor : static_cast<double>(size) * scale / thickness;
        const TreeMapRect rect =
            column ? TreeMapRect{freeRect.x, static_cast<float>(cursor), sliceThickness, static_cast<float>(length)}
                   : TreeMapRect{static_cast<float>(cursor), freeRect.y, static_cast<float>(length), sliceThickness};
        appendChild(parentIndex, lastChild, ref, nonNegative(rect));
        cursor += length;
    }

    if (column) {
        const float right = freeRect.right();
        freeRect.x = static_cast<float>(sliceEdge);
        freeRect.width = std::max(0.0f, right - freeRect.x);
    } else {
        const float bottom = freeRect.bottom();
        freeRect.y = static_cast<float>(sliceEdge);
        freeRect.height = std::max(0.0f, bottom - freeRect.y);
    }
}

void PackedTreeMap::appendChild(uint32_t parentIndex, uint32_t &lastChild, EntryRef ref, const TreeMapRect &rect) {
    const auto index = static_cast<uint32_t>(nodes_.size());
    const auto depth = static_cast<uint16_t>(nodes_[parentIndex].depth + 1);
    nodes_.push_back({rect, rect, ref, kNoNode, kNoNode, depth, false});

    if (lastChild == kNoNode)
        nodes_[parentIndex].firstChild = index;
    else
        nodes_[lastChild].nextSibling = index;
    lastChild = index;
}

EntryRef PackedTreeMap::lookup(float x, float y) const {
    if (nodes_.empty() || !nodes_[0].rect.contains(x, y)) return kNoEntry;

    uint32_t current = 0;
    for (;;) {
        uint32_t child = nodes_[current].firstChild;
        while (child != kNoNode && !nodes_[child].rect.contains(x, y))
            child = nodes_[child].nextSibling;
        if (child == kNoNode) return nodes_[current].ref;
        current = child;
    }
}

} // namespace ldirstat
