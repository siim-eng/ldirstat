#pragma once

#include <cstdint>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"

namespace ldirstat {

struct FlameRect {
    float x1;
    float x2;
    EntryRef ref;
};

struct FlameGraphOptions {
    float width = 0.0f;
    float minNodeWidth = 4.0f;
    uint16_t maxDepth = 64;

    // FlameGraph::build assumes each directory's children are sorted by
    // non-increasing layoutSizeOf(entry). That allows early sibling
    // termination once a child falls below minNodeWidth.
};

// Builds a flame-graph layout from a focused DirEntry subtree.
// Row 0 is the scan root (bottom of the graph). Ancestors of the
// focused entry are emitted as full-width rows so navigation context
// stays visible. Rows above the focused entry contain proportional
// child rects for the focused subtree. X coordinates are relative:
// 0.0 = left edge, 1.0 = right edge of the focused entry's total size.
class FlameGraph {
public:
    static constexpr uint16_t kMaxDepthLimit = 64;

    // Builds the layout from the given focused entry. Child lists are
    // expected to be sorted descending by layoutSizeOf(entry).
    void build(const DirEntryStore &store, EntryRef focus, const FlameGraphOptions &options);

    // Returns the EntryRef at the given relative coordinates,
    // or kNoEntry if nothing is there.
    EntryRef lookup(float x, int row) const;

    int rowCount() const { return static_cast<int>(rows_.size()); }
    const std::vector<FlameRect> &row(int r) const { return rows_[r]; }

private:
    std::vector<std::vector<FlameRect>> rows_;
};

} // namespace ldirstat
