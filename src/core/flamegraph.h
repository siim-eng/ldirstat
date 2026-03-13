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

// Builds a flame-graph layout from a DirEntry tree.
// Row 0 is the root (bottom of the graph). Each row contains rects
// for entries at that depth. X coordinates are relative: 0.0 = left
// edge, 1.0 = right edge of the root's total size.
class FlameGraph {
public:
    static constexpr int kMaxDepth = 64;
    static constexpr float kMinWidth = 1e-4f;

    // Builds the layout from the given root entry.
    void build(const DirEntryStore& store, EntryRef root);

    // Returns the EntryRef at the given relative coordinates,
    // or kNoEntry if nothing is there.
    EntryRef lookup(float x, int row) const;

    int rowCount() const { return static_cast<int>(rows_.size()); }
    const std::vector<FlameRect>& row(int r) const { return rows_[r]; }

private:
    std::vector<std::vector<FlameRect>> rows_;
};

} // namespace ldirstat
