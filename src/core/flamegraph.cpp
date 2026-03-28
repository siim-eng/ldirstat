#include "flamegraph.h"

#include <algorithm>
#include <vector>

namespace ldirstat {

void FlameGraph::build(const DirEntryStore& store, EntryRef focus) {
    rows_.clear();
    if (!focus.valid())
        return;

    const DirEntry& focusEntry = store[focus];
    if (focusEntry.size == 0)
        return;

    std::vector<EntryRef> ancestry;
    for (EntryRef current = focus; current.valid(); current = store[current].parent)
        ancestry.push_back(current);
    std::reverse(ancestry.begin(), ancestry.end());

    rows_.resize(ancestry.size());
    for (size_t row = 0; row < ancestry.size(); ++row)
        rows_[row].push_back({0.0f, 1.0f, ancestry[row]});

    struct Frame {
        EntryRef child;  // next child to process
        float x1;        // current x offset within parent
        float parentWidth;
        uint64_t parentSize;
    };

    Frame stack[kMaxDepth];
    int depth = 0;

    // Seed with the focused entry's children above the ancestry rows.
    EntryRef firstChild = focusEntry.firstChild;
    if (!firstChild.valid())
        return;

    stack[0] = {firstChild, 0.0f, 1.0f, focusEntry.size};
    const int rowOffset = static_cast<int>(ancestry.size());

    while (depth >= 0) {
        Frame& f = stack[depth];

        if (!f.child.valid()) {
            --depth;
            continue;
        }

        int row = rowOffset + depth;
        const DirEntry& entry = store[f.child];
        const uint64_t entrySize = layoutSizeOf(entry);

        // Compute rect x range.
        float width = (f.parentSize > 0)
            ? static_cast<float>(static_cast<double>(entrySize) / f.parentSize) * f.parentWidth
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
