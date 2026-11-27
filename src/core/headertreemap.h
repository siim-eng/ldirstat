#pragma once

#include <cstdint>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "treemapcommon.h"

namespace ldirstat {

struct HeaderTreeMapOptions {
    float width = 0.0f;
    float height = 0.0f;

    // Height of the name strip at the top of each directory.
    float headerHeight = 18.0f;
    // Inset on all four sides of the area below the header.
    float padding = 4.0f;

    // Minimum visible child area in square pixels.
    float minNodeArea = 4.0f;

    // A directory only lays out children when its padded content rect is at
    // least this large in area and on its shorter side.
    float minContentArea = 96.0f;
    float minContentSide = 10.0f;

    uint16_t maxDepth = 64;
};

enum class HeaderTreeMapFrame : uint8_t {
    // Header with padded space available for children.
    Children,
    // Directory with a header and no drawable children.
    HeaderOnly,
    // File or directory too small for a header.
    None,
};

struct HeaderTreeMapNode {
    TreeMapRect rect;
    // Header strip; empty unless frame != None.
    TreeMapRect headerRect;
    // Padded area the children are laid out in; empty unless frame == Children.
    TreeMapRect contentRect;
    EntryRef ref;
    uint32_t firstChild = UINT32_MAX;
    uint32_t nextSibling = UINT32_MAX;
    uint16_t depth = 0;
    HeaderTreeMapFrame frame = HeaderTreeMapFrame::None;
    // Set when the entry has children that are not (all) present as nodes.
    bool childrenCulled = false;
};

// Squarified treemap where each directory is drawn as a frame: a header strip
// with its name and an inset content rect for its files and subdirectories,
// so the parent's background visibly encloses its children.
// coordinates are layout units (pixels) relative to (0, 0).
class HeaderTreeMap {
public:
    static constexpr uint32_t kNoNode = UINT32_MAX;

    void build(const DirEntryStore &store, EntryRef focus, const HeaderTreeMapOptions &options);
    // Deepest entry under the point. Points in a directory's header, padding
    // or unused content space resolve to that directory.
    EntryRef lookup(float x, float y) const;

    // Node 0 is the focus entry. Parents always precede their children.
    const std::vector<HeaderTreeMapNode> &nodes() const { return nodes_; }

private:
    struct Row {
        EntryRef first;
        EntryRef end;
        uint64_t size = 0;
        double area = 0.0;
        uint32_t count = 0;
    };

    void appendNode(const DirEntryStore &store,
                    const HeaderTreeMapOptions &options,
                    uint32_t parentIndex,
                    uint32_t &lastChild,
                    EntryRef ref,
                    const TreeMapRect &rect);
    void layoutChildren(const DirEntryStore &store, const HeaderTreeMapOptions &options, uint32_t nodeIndex);
    static Row gatherRow(const DirEntryStore &store,
                         const HeaderTreeMapOptions &options,
                         EntryRef first,
                         double side,
                         double scale);
    void placeRow(const DirEntryStore &store,
                  const HeaderTreeMapOptions &options,
                  const Row &row,
                  double scale,
                  uint32_t parentIndex,
                  TreeMapRect &freeRect,
                  uint32_t &lastChild);

    std::vector<HeaderTreeMapNode> nodes_;
};

} // namespace ldirstat
