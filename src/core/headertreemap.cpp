#include "headertreemap.h"

#include <algorithm>
#include <limits>

namespace ldirstat {

namespace {

constexpr size_t kMinReservedNodes = 4096;

uint64_t childrenLayoutSize(const DirEntryStore &store, const DirEntry &dir) {
    uint64_t total = 0;
    for (EntryRef ref = dir.firstChild; ref.valid(); ref = store[ref].nextSibling)
        total += layoutSizeOf(store[ref]);
    return total;
}

EntryRef skipEmptySiblings(const DirEntryStore &store, EntryRef ref) {
    while (ref.valid() && layoutSizeOf(store[ref]) == 0)
        ref = store[ref].nextSibling;
    return ref;
}

bool fitsHeader(const TreeMapRect &rect, const HeaderTreeMapOptions &options) {
    return rect.height >= options.headerHeight + options.padding && rect.width >= options.headerHeight;
}

TreeMapRect paddedContent(const TreeMapRect &rect, const HeaderTreeMapOptions &options) {
    const float pad = options.padding;
    return TreeMapRect{
        rect.x + pad,
        rect.y + options.headerHeight + pad,
        rect.width - 2.0f * pad,
        rect.height - options.headerHeight - 2.0f * pad,
    };
}

bool fitsChildren(const TreeMapRect &content, const HeaderTreeMapOptions &options) {
    return !content.empty() && content.area() >= options.minContentArea
           && std::min(content.width, content.height) >= options.minContentSide;
}

} // namespace

void HeaderTreeMap::build(const DirEntryStore &store, EntryRef focus, const HeaderTreeMapOptions &options) {
    nodes_.clear();
    if (!focus.valid() || options.width <= 0.0f || options.height <= 0.0f) return;

    const DirEntry &focusEntry = store[focus];
    const uint64_t focusSize = focusEntry.firstChild.valid() ? childrenLayoutSize(store, focusEntry) : layoutSizeOf(focusEntry);
    if (focusSize == 0) return;

    nodes_.reserve(std::max<size_t>(kMinReservedNodes, static_cast<size_t>(focusEntry.childCount) * 2 + 1));

    uint32_t unusedSibling = kNoNode;
    appendNode(store, options, kNoNode, unusedSibling, focus, TreeMapRect{0.0f, 0.0f, options.width, options.height});

    // The growing node vector doubles as a breadth-first queue.
    for (uint32_t index = 0; index < nodes_.size(); ++index)
        layoutChildren(store, options, index);
}

void HeaderTreeMap::appendNode(const DirEntryStore &store,
                               const HeaderTreeMapOptions &options,
                               uint32_t parentIndex,
                               uint32_t &lastChild,
                               EntryRef ref,
                               const TreeMapRect &rect) {
    const DirEntry &entry = store[ref];
    const auto index = static_cast<uint32_t>(nodes_.size());

    HeaderTreeMapNode node;
    node.rect = rect;
    node.ref = ref;
    node.depth = parentIndex == kNoNode ? 0 : static_cast<uint16_t>(nodes_[parentIndex].depth + 1);

    const bool hasChildren = entry.firstChild.valid();
    if (entry.isDir() && fitsHeader(rect, options)) {
        node.headerRect = TreeMapRect{rect.x, rect.y, rect.width, options.headerHeight};
        node.frame = HeaderTreeMapFrame::HeaderOnly;

        const TreeMapRect content = paddedContent(rect, options);
        if (hasChildren && node.depth < options.maxDepth && fitsChildren(content, options)) {
            node.contentRect = content;
            node.frame = HeaderTreeMapFrame::Children;
        }
    }
    node.childrenCulled = hasChildren && node.frame != HeaderTreeMapFrame::Children;

    nodes_.push_back(node);
    if (parentIndex == kNoNode) return;

    if (lastChild == kNoNode)
        nodes_[parentIndex].firstChild = index;
    else
        nodes_[lastChild].nextSibling = index;
    lastChild = index;
}

void HeaderTreeMap::layoutChildren(const DirEntryStore &store, const HeaderTreeMapOptions &options, uint32_t nodeIndex) {
    if (nodes_[nodeIndex].frame != HeaderTreeMapFrame::Children) return;

    const DirEntry &entry = store[nodes_[nodeIndex].ref];
    uint64_t sizeLeft = childrenLayoutSize(store, entry);
    if (sizeLeft == 0) {
        // Nothing to frame: present it like any other childless directory.
        nodes_[nodeIndex].frame = HeaderTreeMapFrame::HeaderOnly;
        nodes_[nodeIndex].contentRect = {};
        return;
    }

    TreeMapRect freeRect = nodes_[nodeIndex].contentRect;
    uint32_t lastChild = kNoNode;
    EntryRef next = skipEmptySiblings(store, entry.firstChild);

    while (next.valid() && sizeLeft > 0) {
        if (freeRect.empty()) {
            nodes_[nodeIndex].childrenCulled = true;
            break;
        }

        const double scale = static_cast<double>(freeRect.area()) / static_cast<double>(sizeLeft);
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

        const Row row = gatherRow(store, options, next, std::min(freeRect.width, freeRect.height), scale);
        placeRow(store, options, row, scale, nodeIndex, freeRect, lastChild);
        sizeLeft -= row.size;
        next = skipEmptySiblings(store, row.end);
    }
}

HeaderTreeMap::Row HeaderTreeMap::gatherRow(const DirEntryStore &store,
                                            const HeaderTreeMapOptions &options,
                                            EntryRef first,
                                            double side,
                                            double scale) {
    Row row;
    row.first = first;
    row.end = first;
    double minArea = std::numeric_limits<double>::infinity();
    double maxArea = 0.0;
    double rowWorst = std::numeric_limits<double>::infinity();

    for (; row.end.valid(); row.end = store[row.end].nextSibling) {
        const uint64_t size = layoutSizeOf(store[row.end]);
        if (size == 0) continue;

        const double area = static_cast<double>(size) * scale;
        if (area < options.minNodeArea) break;

        const double trialArea = row.area + area;
        const double trialMin = std::min(minArea, area);
        const double trialMax = std::max(maxArea, area);
        const double trialWorst = squarifyWorstAspect(trialArea, trialMin, trialMax, side);
        if (row.count > 0 && trialWorst > rowWorst) break;

        row.area = trialArea;
        row.size += size;
        ++row.count;
        minArea = trialMin;
        maxArea = trialMax;
        rowWorst = trialWorst;
    }

    return row;
}

void HeaderTreeMap::placeRow(const DirEntryStore &store,
                             const HeaderTreeMapOptions &options,
                             const Row &row,
                             double scale,
                             uint32_t parentIndex,
                             TreeMapRect &freeRect,
                             uint32_t &lastChild) {
    if (freeRect.width >= freeRect.height) {
        // Column along the left edge of the free rect.
        const double columnWidth = row.area / freeRect.height;
        const auto columnRight = static_cast<float>(freeRect.x + columnWidth);
        const float freeBottom = freeRect.bottom();
        double y = freeRect.y;
        uint32_t placed = 0;

        for (EntryRef ref = row.first; ref != row.end; ref = store[ref].nextSibling) {
            const uint64_t size = layoutSizeOf(store[ref]);
            if (size == 0) continue;

            ++placed;
            const bool last = placed == row.count;
            const auto top = static_cast<float>(y);
            const float bottom = last ? freeBottom : static_cast<float>(y + static_cast<double>(size) * scale / columnWidth);
            appendNode(store,
                       options,
                       parentIndex,
                       lastChild,
                       ref,
                       TreeMapRect{freeRect.x, top, std::max(0.0f, columnRight - freeRect.x), std::max(0.0f, bottom - top)});
            y = bottom;
        }

        const float freeRight = freeRect.right();
        freeRect.x = columnRight;
        freeRect.width = std::max(0.0f, freeRight - columnRight);
        return;
    }

    // Strip along the top edge of the free rect.
    const double stripHeight = row.area / freeRect.width;
    const auto stripBottom = static_cast<float>(freeRect.y + stripHeight);
    const float freeRight = freeRect.right();
    double x = freeRect.x;
    uint32_t placed = 0;

    for (EntryRef ref = row.first; ref != row.end; ref = store[ref].nextSibling) {
        const uint64_t size = layoutSizeOf(store[ref]);
        if (size == 0) continue;

        ++placed;
        const bool last = placed == row.count;
        const auto left = static_cast<float>(x);
        const float right = last ? freeRight : static_cast<float>(x + static_cast<double>(size) * scale / stripHeight);
        appendNode(store,
                   options,
                   parentIndex,
                   lastChild,
                   ref,
                   TreeMapRect{left, freeRect.y, std::max(0.0f, right - left), std::max(0.0f, stripBottom - freeRect.y)});
        x = right;
    }

    const float freeBottom = freeRect.bottom();
    freeRect.y = stripBottom;
    freeRect.height = std::max(0.0f, freeBottom - stripBottom);
}

EntryRef HeaderTreeMap::lookup(float x, float y) const {
    if (nodes_.empty() || !nodes_[0].rect.contains(x, y)) return kNoEntry;

    uint32_t current = 0;
    for (;;) {
        uint32_t hit = kNoNode;
        if (nodes_[current].contentRect.contains(x, y)) {
            for (uint32_t child = nodes_[current].firstChild; child != kNoNode; child = nodes_[child].nextSibling) {
                if (nodes_[child].rect.contains(x, y)) {
                    hit = child;
                    break;
                }
            }
        }
        if (hit == kNoNode) return nodes_[current].ref;
        current = hit;
    }
}

} // namespace ldirstat
