#include "treemap.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace ldirstat {

namespace {

double worstAspect(double rowArea, double minArea, double maxArea, double shortSide) {
    if (rowArea <= 0.0 || minArea <= 0.0 || shortSide <= 0.0) return std::numeric_limits<double>::infinity();

    const double shortSideSq = shortSide * shortSide;
    const double rowAreaSq = rowArea * rowArea;
    const double worstMax = (shortSideSq * maxArea) / rowAreaSq;
    const double worstMin = rowAreaSq / (shortSideSq * minArea);
    return std::max(worstMax, worstMin);
}

bool canDescend(const TreeMapRect &rect, const TreeMapOptions &options) {
    return rect.width > 0.0f && rect.height > 0.0f && rect.area() >= options.minDirAreaForChildren
           && std::min(rect.width, rect.height) >= options.minDirSideForChildren;
}

TreeMapRect contentRectFor(const DirEntry &entry, const TreeMapRect &rect, const TreeMapOptions &options) {
    if (!entry.isDir() || !entry.firstChild.valid() || options.directoryHeaderHeight <= 0.0f) return rect;

    const float headerHeight = std::min(options.directoryHeaderHeight, rect.height);
    return TreeMapRect{rect.x, rect.y + headerHeight, rect.width, rect.height - headerHeight};
}

TreeMapRect clampRect(const TreeMapRect &rect) {
    return TreeMapRect{
        rect.x,
        rect.y,
        std::max(rect.width, 0.0f),
        std::max(rect.height, 0.0f),
    };
}

uint64_t totalTreemapSizeOfChildren(const DirEntryStore &store, EntryRef firstChild) {
    uint64_t totalSize = 0;
    for (EntryRef childRef = firstChild; childRef.valid(); childRef = store[childRef].nextSibling)
        totalSize += layoutSizeOf(store[childRef]);
    return totalSize;
}

} // namespace

void TreeMap::build(const DirEntryStore &store, EntryRef focus, const TreeMapOptions &options) {
    nodes_.clear();
    stack_.clear();
    pendingChildren_.clear();
    rowEntries_.clear();

    if (!focus.valid() || options.width <= 0.0f || options.height <= 0.0f) return;

    const DirEntry &rootEntry = store[focus];
    const uint64_t rootTreemapSize =
        rootEntry.firstChild.valid() ? totalTreemapSizeOfChildren(store, rootEntry.firstChild) : layoutSizeOf(rootEntry);
    if (rootTreemapSize == 0) return;

    const size_t reserveCount =
        std::max(nodes_.capacity(), std::max<size_t>(4096, static_cast<size_t>(rootEntry.childCount) * 2 + 1));
    nodes_.reserve(reserveCount);

    TreeMapRect rootRect{0.0f, 0.0f, options.width, options.height};
    nodes_.push_back({
        rootRect,
        contentRectFor(rootEntry, rootRect, options),
        rootRect,
        focus,
        kNoNode,
        kNoNode,
        0,
        false,
    });

    if (!rootEntry.firstChild.valid() || options.maxDepth == 0) return;

    if (!canDescend(nodes_[0].contentRect, options)) {
        nodes_[0].childrenCulled = true;
        return;
    }

    stack_.push_back({0, nodes_[0].contentRect, rootEntry.firstChild, rootTreemapSize});
    while (!stack_.empty()) {
        const Frame frame = stack_.back();
        stack_.pop_back();
        processFrame(store, options, frame);
    }
}

void TreeMap::processFrame(const DirEntryStore &store, const TreeMapOptions &options, const Frame &frame) {
    const uint16_t childDepth = static_cast<uint16_t>(nodes_[frame.parentNode].depth + 1);
    TreeMapRect remainingRect = frame.rect;
    uint64_t remainingSize = frame.totalSize;
    EntryRef childRef = frame.firstChild;
    uint32_t prevVisibleChild = kNoNode;
    pendingChildren_.clear();

    while (childRef.valid() && remainingSize > 0) {
        trimZeroSized(store, childRef);
        if (!childRef.valid() || !hasRemainingRect(remainingRect)) break;

        const double scale = areaScale(remainingRect, remainingSize);
        if (isTooSmall(store[childRef], scale, options)) {
            nodes_[frame.parentNode].childrenCulled = true;
            break;
        }

        const RowState row = collectRow(store, options, childRef, remainingRect, remainingSize);
        if (row.empty()) {
            nodes_[frame.parentNode].childrenCulled = true;
            break;
        }

        emitRow(store, options, frame.parentNode, childDepth, remainingRect, row, prevVisibleChild);
        remainingSize -= row.size;
        childRef = row.nextChild;
    }

    if (prevVisibleChild != kNoNode) nodes_[frame.parentNode].labelRect = clampRect(remainingRect);

    pushPendingChildren();
    finalizeFrame(store, frame.parentNode, childRef);
}

TreeMap::RowState TreeMap::collectRow(const DirEntryStore &store,
                                      const TreeMapOptions &options,
                                      EntryRef firstChild,
                                      const TreeMapRect &remainingRect,
                                      uint64_t remainingSize) {
    rowEntries_.clear();

    RowState row;
    row.nextChild = firstChild;

    const double shortSide = std::min(remainingRect.width, remainingRect.height);
    const double scale = areaScale(remainingRect, remainingSize);
    double rowMinArea = std::numeric_limits<double>::infinity();
    double rowMaxArea = 0.0;

    while (row.nextChild.valid()) {
        const DirEntry &candidate = store[row.nextChild];
        const uint64_t candidateSize = layoutSizeOf(candidate);
        if (candidateSize == 0) {
            row.nextChild = candidate.nextSibling;
            continue;
        }

        const double candidateArea = static_cast<double>(candidateSize) * scale;
        if (candidateArea < options.minNodeArea) break;

        const double trialArea = row.area + candidateArea;
        const double trialMinArea = std::min(rowMinArea, candidateArea);
        const double trialMaxArea = std::max(rowMaxArea, candidateArea);

        if (!rowEntries_.empty()
            && worstAspect(row.area, rowMinArea, rowMaxArea, shortSide)
                   < worstAspect(trialArea, trialMinArea, trialMaxArea, shortSide)) {
            break;
        }

        rowEntries_.push_back({row.nextChild, candidateArea, candidateSize});
        row.area = trialArea;
        row.size += candidateSize;
        rowMinArea = trialMinArea;
        rowMaxArea = trialMaxArea;
        row.nextChild = candidate.nextSibling;
    }

    return row;
}

void TreeMap::emitRow(const DirEntryStore &store,
                      const TreeMapOptions &options,
                      uint32_t parentNode,
                      uint16_t childDepth,
                      TreeMapRect &remainingRect,
                      const RowState &row,
                      uint32_t &prevVisibleChild) {
    if (remainingRect.width >= remainingRect.height) {
        emitVerticalRow(store, options, parentNode, childDepth, remainingRect, row, prevVisibleChild);
        return;
    }

    emitHorizontalRow(store, options, parentNode, childDepth, remainingRect, row, prevVisibleChild);
}

void TreeMap::emitVerticalRow(const DirEntryStore &store,
                              const TreeMapOptions &options,
                              uint32_t parentNode,
                              uint16_t childDepth,
                              TreeMapRect &remainingRect,
                              const RowState &row,
                              uint32_t &prevVisibleChild) {
    const double sliceWidth = row.area / remainingRect.height;
    const double sliceRight = remainingRect.x + sliceWidth;
    const float remainingBottom = remainingRect.bottom();
    double cursorY = remainingRect.y;

    for (size_t i = 0; i < rowEntries_.size(); ++i) {
        double itemHeight = rowEntries_[i].area / sliceWidth;
        if (i + 1 == rowEntries_.size()) itemHeight = remainingBottom - cursorY;

        const TreeMapRect childRect = clampRect({
            remainingRect.x,
            static_cast<float>(cursorY),
            static_cast<float>(sliceRight - remainingRect.x),
            static_cast<float>(itemHeight),
        });
        appendNode(store, options, parentNode, prevVisibleChild, rowEntries_[i].ref, childDepth, childRect);
        cursorY += itemHeight;
    }

    const float oldRight = remainingRect.right();
    remainingRect.x = static_cast<float>(sliceRight);
    remainingRect.width = std::max(0.0f, oldRight - remainingRect.x);
}

void TreeMap::emitHorizontalRow(const DirEntryStore &store,
                                const TreeMapOptions &options,
                                uint32_t parentNode,
                                uint16_t childDepth,
                                TreeMapRect &remainingRect,
                                const RowState &row,
                                uint32_t &prevVisibleChild) {
    const double sliceHeight = row.area / remainingRect.width;
    const double sliceBottom = remainingRect.y + sliceHeight;
    const float remainingRight = remainingRect.right();
    double cursorX = remainingRect.x;

    for (size_t i = 0; i < rowEntries_.size(); ++i) {
        double itemWidth = rowEntries_[i].area / sliceHeight;
        if (i + 1 == rowEntries_.size()) itemWidth = remainingRight - cursorX;

        const TreeMapRect childRect = clampRect({
            static_cast<float>(cursorX),
            remainingRect.y,
            static_cast<float>(itemWidth),
            static_cast<float>(sliceBottom - remainingRect.y),
        });
        appendNode(store, options, parentNode, prevVisibleChild, rowEntries_[i].ref, childDepth, childRect);
        cursorX += itemWidth;
    }

    const float oldBottom = remainingRect.bottom();
    remainingRect.y = static_cast<float>(sliceBottom);
    remainingRect.height = std::max(0.0f, oldBottom - remainingRect.y);
}

void TreeMap::appendNode(const DirEntryStore &store,
                         const TreeMapOptions &options,
                         uint32_t parentNode,
                         uint32_t &prevVisibleChild,
                         EntryRef ref,
                         uint16_t childDepth,
                         const TreeMapRect &rect) {
    const DirEntry &entry = store[ref];
    const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());

    nodes_.push_back({
        rect,
        contentRectFor(entry, rect, options),
        rect,
        ref,
        kNoNode,
        kNoNode,
        childDepth,
        false,
    });

    if (prevVisibleChild == kNoNode)
        nodes_[parentNode].firstChild = nodeIndex;
    else
        nodes_[prevVisibleChild].nextSibling = nodeIndex;
    prevVisibleChild = nodeIndex;

    if (!entry.isDir() || !entry.firstChild.valid()) return;

    if (childDepth >= options.maxDepth || !canDescend(nodes_[nodeIndex].contentRect, options)) {
        nodes_[nodeIndex].childrenCulled = true;
        return;
    }

    const uint64_t childTreemapSize = totalTreemapSizeOfChildren(store, entry.firstChild);
    if (childTreemapSize == 0) return;

    pendingChildren_.push_back({nodeIndex, nodes_[nodeIndex].contentRect, entry.firstChild, childTreemapSize});
}

void TreeMap::pushPendingChildren() {
    for (auto it = pendingChildren_.rbegin(); it != pendingChildren_.rend(); ++it)
        stack_.push_back(*it);
}

void TreeMap::finalizeFrame(const DirEntryStore &store, uint32_t parentNode, EntryRef childRef) {
    trimZeroSized(store, childRef);
    if (childRef.valid()) nodes_[parentNode].childrenCulled = true;
}

void TreeMap::trimZeroSized(const DirEntryStore &store, EntryRef &childRef) {
    while (childRef.valid() && layoutSizeOf(store[childRef]) == 0)
        childRef = store[childRef].nextSibling;
}

bool TreeMap::hasRemainingRect(const TreeMapRect &rect) {
    return rect.width > 0.0f && rect.height > 0.0f;
}

double TreeMap::areaScale(const TreeMapRect &rect, uint64_t remainingSize) {
    const double remainingArea = static_cast<double>(rect.width) * rect.height;
    return remainingArea / static_cast<double>(remainingSize);
}

bool TreeMap::isTooSmall(const DirEntry &entry, double scale, const TreeMapOptions &options) {
    return static_cast<double>(layoutSizeOf(entry)) * scale < options.minNodeArea;
}

EntryRef TreeMap::lookup(float x, float y) const {
    if (nodes_.empty() || !nodes_[0].rect.contains(x, y)) return kNoEntry;

    uint32_t current = 0;
    for (size_t depth = 0; depth < nodes_.size(); ++depth) {
        uint32_t child = nodes_[current].firstChild;
        uint32_t hit = kNoNode;

        while (child != kNoNode) {
            if (nodes_[child].rect.contains(x, y)) {
                hit = child;
                break;
            }
            child = nodes_[child].nextSibling;
        }

        if (hit == kNoNode) return nodes_[current].ref;

        current = hit;
    }

    return kNoEntry;
}

} // namespace ldirstat
