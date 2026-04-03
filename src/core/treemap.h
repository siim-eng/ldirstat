#pragma once

#include <cstdint>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"

namespace ldirstat {

struct TreeMapRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    float right() const { return x + width; }
    float bottom() const { return y + height; }
    float area() const { return width * height; }

    bool contains(float px, float py) const { return px >= x && py >= y && px < right() && py < bottom(); }
};

struct TreeMapOptions {
    float width = 0.0f;
    float height = 0.0f;

    // Directory label/header reservation in layout units (typically pixels).
    // Set to 0 for the traditional fully packed treemap.
    float directoryHeaderHeight = 0.0f;

    // Stop emitting additional siblings when their area drops below this.
    float minNodeArea = 4.0f;

    // Skip descending into directories whose child-content rect is too small.
    float minDirAreaForChildren = 64.0f;
    float minDirSideForChildren = 8.0f;

    uint16_t maxDepth = 64;
};

struct TreeMapNode {
    TreeMapRect rect;
    TreeMapRect contentRect;
    TreeMapRect labelRect;
    EntryRef ref;
    uint32_t firstChild = UINT32_MAX;
    uint32_t nextSibling = UINT32_MAX;
    uint16_t depth = 0;
    bool childrenCulled = false;
};

// Squarified treemap layout for a focused DirEntry subtree.
// The layout is Qt-free and stores both the outer node rect and the
// child-content rect so multiple renderers can share the same geometry.
class TreeMap {
public:
    static constexpr uint32_t kNoNode = UINT32_MAX;

    void build(const DirEntryStore &store, EntryRef focus, const TreeMapOptions &options);
    EntryRef lookup(float x, float y) const;

    const std::vector<TreeMapNode> &nodes() const { return nodes_; }

private:
    struct RowEntry {
        EntryRef ref;
        double area = 0.0;
        uint64_t size = 0;
    };

    struct Frame {
        uint32_t parentNode = kNoNode;
        TreeMapRect rect;
        EntryRef firstChild;
        uint64_t totalSize = 0;
    };

    struct RowState {
        double area = 0.0;
        uint64_t size = 0;
        EntryRef nextChild;

        bool empty() const { return size == 0; }
    };

    void processFrame(const DirEntryStore &store, const TreeMapOptions &options, const Frame &frame);
    RowState collectRow(const DirEntryStore &store,
                        const TreeMapOptions &options,
                        EntryRef firstChild,
                        const TreeMapRect &remainingRect,
                        uint64_t remainingSize);
    void emitRow(const DirEntryStore &store,
                 const TreeMapOptions &options,
                 uint32_t parentNode,
                 uint16_t childDepth,
                 TreeMapRect &remainingRect,
                 const RowState &row,
                 uint32_t &prevVisibleChild);
    void emitVerticalRow(const DirEntryStore &store,
                         const TreeMapOptions &options,
                         uint32_t parentNode,
                         uint16_t childDepth,
                         TreeMapRect &remainingRect,
                         const RowState &row,
                         uint32_t &prevVisibleChild);
    void emitHorizontalRow(const DirEntryStore &store,
                           const TreeMapOptions &options,
                           uint32_t parentNode,
                           uint16_t childDepth,
                           TreeMapRect &remainingRect,
                           const RowState &row,
                           uint32_t &prevVisibleChild);
    void appendNode(const DirEntryStore &store,
                    const TreeMapOptions &options,
                    uint32_t parentNode,
                    uint32_t &prevVisibleChild,
                    EntryRef ref,
                    uint16_t childDepth,
                    const TreeMapRect &rect);
    void pushPendingChildren();
    void finalizeFrame(const DirEntryStore &store, uint32_t parentNode, EntryRef childRef);

    static void trimZeroSized(const DirEntryStore &store, EntryRef &childRef);
    static bool hasRemainingRect(const TreeMapRect &rect);
    static double areaScale(const TreeMapRect &rect, uint64_t remainingSize);
    static bool isTooSmall(const DirEntry &entry, double scale, const TreeMapOptions &options);

    std::vector<TreeMapNode> nodes_;
    std::vector<Frame> stack_;
    std::vector<Frame> pendingChildren_;
    // Scratch storage reused across collectRow() + emitRow() to avoid per-row
    // allocations while keeping the current row materialized between the two steps.
    std::vector<RowEntry> rowEntries_;
};

} // namespace ldirstat
