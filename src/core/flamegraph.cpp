#include "flamegraph.h"

#include <algorithm>

namespace ldirstat {

void FlameGraph::build(const DirEntryStore& store, EntryRef root) {
    rows_.clear();

    const DirEntry& rootEntry = store[root];
    if (rootEntry.size == 0)
        return;

    struct Frame {
        EntryRef child;  // next child to process
        float x1;        // current x offset within parent
        float parentWidth;
        uint64_t parentSize;
    };

    // Push root rect.
    rows_.resize(1);
    rows_[0].push_back({0.0f, 1.0f, root});

    Frame stack[kMaxDepth];
    int depth = 0;

    // Seed with root's children.
    EntryRef firstChild = rootEntry.firstChild;
    if (!firstChild.valid())
        return;

    stack[0] = {firstChild, 0.0f, 1.0f, rootEntry.size};

    while (depth >= 0) {
        Frame& f = stack[depth];

        if (!f.child.valid()) {
            --depth;
            continue;
        }

        int row = depth + 1;
        const DirEntry& entry = store[f.child];

        // Compute rect x range.
        float width = (f.parentSize > 0)
            ? static_cast<float>(static_cast<double>(entry.size) / f.parentSize) * f.parentWidth
            : 0.0f;
        float x1 = f.x1;
        float x2 = x1 + width;

        // Advance to next sibling before potential descent.
        EntryRef current = f.child;
        f.child = entry.nextSibling;
        f.x1 = x2;

        // Cull tiny rects.
        if (width < kMinWidth)
            continue;

        // Emit rect.
        if (row >= static_cast<int>(rows_.size()))
            rows_.resize(row + 1);
        rows_[row].push_back({x1, x2, current});

        // Descend into directories with children.
        if (entry.isDir() && entry.firstChild.valid() && depth + 1 < kMaxDepth) {
            ++depth;
            stack[depth] = {entry.firstChild, x1, width, entry.size};
        }
    }
}

EntryRef FlameGraph::lookup(float x, int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return kNoEntry;

    const auto& rects = rows_[row];
    if (rects.empty())
        return kNoEntry;

    // Binary search: find last rect with x1 <= x.
    auto it = std::upper_bound(rects.begin(), rects.end(), x,
        [](float val, const FlameRect& r) { return val < r.x1; });

    if (it == rects.begin())
        return kNoEntry;

    --it;
    if (x >= it->x1 && x < it->x2)
        return it->ref;

    return kNoEntry;
}

} // namespace ldirstat
