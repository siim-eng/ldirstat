#pragma once

#include <cstdint>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "treemapcommon.h"

namespace ldirstat {

struct PackedTreeMapOptions {
    float width = 0.0f;
    float height = 0.0f;

    // Minimum visible child area in square pixels.
    float minNodeArea = 16.0f;
    // Minimum width and height of a visible child, so tiny items do not
    // collapse into border-only slivers.
    float minNodeSide = 3.0f;

    // A directory only gets its children laid out when its rect is at least
    // this large in area and on its shorter side.
    float minDirAreaForChildren = 96.0f;
    float minDirSideForChildren = 10.0f;

    uint16_t maxDepth = 64;
};

struct PackedTreeMapNode {
    TreeMapRect rect;
    // Where the entry's label goes: the full rect for files and leaf
    // directories, the space left over after the visible children otherwise.
    TreeMapRect labelRect;
    EntryRef ref;
    uint32_t firstChild = UINT32_MAX;
    uint32_t nextSibling = UINT32_MAX;
    uint16_t depth = 0;
    // Set when the entry has children that are not (all) present as nodes.
    bool childrenCulled = false;
};

// Fully packed squarified treemap: every directory's children cover the
// directory's whole rect, with no room reserved for headers or padding.
// coordinates are layout units (pixels) relative to (0, 0).
class PackedTreeMap {
public:
    static constexpr uint32_t kNoNode = UINT32_MAX;

    void build(const DirEntryStore &store, EntryRef focus, const PackedTreeMapOptions &options);
    // Deepest entry whose rect contains the point, or kNoEntry outside the map.
    EntryRef lookup(float x, float y) const;

    // Node 0 is the focus entry. Parents always precede their children.
    const std::vector<PackedTreeMapNode> &nodes() const { return nodes_; }

private:
    // A squarify row is a run of consecutive siblings [first, end).
    struct Row {
        EntryRef first;
        EntryRef end;
        uint64_t size = 0;
        double area = 0.0;
        double minArea = 0.0;
        uint32_t count = 0;
    };

    void layoutChildren(const DirEntryStore &store, const PackedTreeMapOptions &options, uint32_t nodeIndex);
    static Row gatherRow(const DirEntryStore &store,
                         const PackedTreeMapOptions &options,
                         EntryRef first,
                         double side,
                         double scale);
    void placeRow(const DirEntryStore &store,
                  const Row &row,
                  double scale,
                  uint32_t parentIndex,
                  TreeMapRect &freeRect,
                  uint32_t &lastChild);
    void appendChild(uint32_t parentIndex, uint32_t &lastChild, EntryRef ref, const TreeMapRect &rect);

    std::vector<PackedTreeMapNode> nodes_;
};

} // namespace ldirstat
