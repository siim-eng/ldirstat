#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "headertreemap.h"

namespace {

using ldirstat::DirEntryStore;
using ldirstat::EntryRef;
using ldirstat::EntryType;
using ldirstat::HeaderTreeMap;
using ldirstat::HeaderTreeMapFrame;
using ldirstat::HeaderTreeMapNode;
using ldirstat::HeaderTreeMapOptions;
using ldirstat::TreeMapRect;

constexpr float kEpsilon = 0.01f;
constexpr float kHeader = 20.0f;
constexpr float kPadding = 4.0f;

EntryRef addTestEntry(DirEntryStore &entries, DirEntryStore::AppendCursor &cursor, EntryType type, uint64_t size) {
    const EntryRef ref = entries.add(cursor);
    auto &entry = entries[ref];
    entry.type = type;
    entry.size = size;
    entry.hardLinks = 1;
    return ref;
}

void linkChildren(DirEntryStore &entries, EntryRef parentRef, const std::vector<EntryRef> &children) {
    auto &parent = entries[parentRef];
    parent.firstChild = ldirstat::kNoEntry;
    parent.childCount = static_cast<uint32_t>(children.size());

    EntryRef previous = ldirstat::kNoEntry;
    for (const EntryRef childRef : children) {
        auto &child = entries[childRef];
        child.parent = parentRef;
        child.nextSibling = ldirstat::kNoEntry;

        if (!parent.firstChild.valid())
            parent.firstChild = childRef;
        else
            entries[previous].nextSibling = childRef;

        previous = childRef;
    }
}

void linkChildren(DirEntryStore &entries, EntryRef parentRef, std::initializer_list<EntryRef> children) {
    linkChildren(entries, parentRef, std::vector<EntryRef>(children));
}

// Deterministic pseudo-random tree with children sorted by size, like the
// scanner produces. Returns the root directory.
class RandomTree {
public:
    explicit RandomTree(uint32_t seed)
        : state_(seed) {}

    EntryRef build(DirEntryStore &entries, DirEntryStore::AppendCursor &cursor, int depth) {
        const EntryRef dir = addTestEntry(entries, cursor, EntryType::Directory, 0);
        const uint32_t childCount = 1 + next() % 12;
        std::vector<EntryRef> children;
        uint64_t total = 0;
        for (uint32_t i = 0; i < childCount; ++i) {
            EntryRef child;
            if (depth > 0 && next() % 3 == 0) {
                child = build(entries, cursor, depth - 1);
            } else {
                const uint64_t size = (next() % 5 == 0) ? 0 : 1 + next() % 100000;
                child = addTestEntry(entries, cursor, EntryType::File, size);
            }
            total += entries[child].size;
            children.push_back(child);
        }
        std::stable_sort(children.begin(), children.end(), [&entries](EntryRef a, EntryRef b) {
            return entries[a].size > entries[b].size;
        });
        linkChildren(entries, dir, children);
        entries[dir].size = total;
        return dir;
    }

private:
    uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_ >> 8;
    }

    uint32_t state_;
};

bool within(const TreeMapRect &inner, const TreeMapRect &outer) {
    return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.right() <= outer.right() + kEpsilon
           && inner.bottom() <= outer.bottom() + kEpsilon;
}

float overlapArea(const TreeMapRect &a, const TreeMapRect &b) {
    const float w = std::min(a.right(), b.right()) - std::max(a.x, b.x);
    const float h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
    return (w > 0.0f && h > 0.0f) ? w * h : 0.0f;
}

std::vector<uint32_t> childIndices(const HeaderTreeMap &map, uint32_t parent) {
    std::vector<uint32_t> result;
    for (uint32_t child = map.nodes()[parent].firstChild; child != HeaderTreeMap::kNoNode; child = map.nodes()[child].nextSibling)
        result.push_back(child);
    return result;
}

const HeaderTreeMapNode *findNode(const HeaderTreeMap &map, EntryRef ref) {
    for (const HeaderTreeMapNode &node : map.nodes()) {
        if (node.ref == ref) return &node;
    }
    return nullptr;
}

HeaderTreeMapOptions makeOptions(float width, float height) {
    HeaderTreeMapOptions options;
    options.width = width;
    options.height = height;
    options.headerHeight = kHeader;
    options.padding = kPadding;
    options.minNodeArea = 4.0f;
    options.minContentSide = 14.0f;
    options.minContentArea = 240.0f;
    return options;
}

} // namespace

TEST_CASE("header treemap: root gets a header and a padded content rect") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 10);
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 10);
    linkChildren(store, root, {file});

    HeaderTreeMap map;
    map.build(store, root, makeOptions(300.0f, 200.0f));

    REQUIRE(map.nodes().size() == 2);
    const HeaderTreeMapNode &rootNode = map.nodes()[0];
    CHECK(rootNode.frame == HeaderTreeMapFrame::Children);
    CHECK(rootNode.headerRect.x == 0.0f);
    CHECK(rootNode.headerRect.y == 0.0f);
    CHECK(rootNode.headerRect.width == 300.0f);
    CHECK(rootNode.headerRect.height == kHeader);
    CHECK(rootNode.contentRect.x == doctest::Approx(kPadding));
    CHECK(rootNode.contentRect.y == doctest::Approx(kHeader + kPadding));
    CHECK(rootNode.contentRect.width == doctest::Approx(300.0f - 2.0f * kPadding));
    CHECK(rootNode.contentRect.height == doctest::Approx(200.0f - kHeader - 2.0f * kPadding));

    // A lone child fills the whole content rect, never the header or padding.
    const HeaderTreeMapNode &fileNode = map.nodes()[1];
    CHECK(fileNode.frame == HeaderTreeMapFrame::None);
    CHECK(fileNode.rect.x == doctest::Approx(rootNode.contentRect.x));
    CHECK(fileNode.rect.y == doctest::Approx(rootNode.contentRect.y));
    CHECK(fileNode.rect.area() == doctest::Approx(rootNode.contentRect.area()));
}

TEST_CASE("header treemap: child areas are proportional to layout sizes within the content rect") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 1000);
    const EntryRef a = addTestEntry(store, cursor, EntryType::File, 500);
    const EntryRef b = addTestEntry(store, cursor, EntryType::File, 250);
    const EntryRef c = addTestEntry(store, cursor, EntryType::File, 150);
    const EntryRef d = addTestEntry(store, cursor, EntryType::File, 300);
    store[d].hardLinks = 3; // counts as 100 for layout
    linkChildren(store, root, {a, b, c, d});

    HeaderTreeMap map;
    map.build(store, root, makeOptions(400.0f, 300.0f));

    const std::vector<uint32_t> children = childIndices(map, 0);
    REQUIRE(children.size() == 4);

    const double contentArea = map.nodes()[0].contentRect.area();
    const double totalSize = 1000.0;
    double sumArea = 0.0;
    for (const uint32_t index : children) {
        const HeaderTreeMapNode &node = map.nodes()[index];
        const double expected = static_cast<double>(ldirstat::layoutSizeOf(store[node.ref])) / totalSize * contentArea;
        CHECK(node.rect.area() == doctest::Approx(expected).epsilon(0.001));
        sumArea += node.rect.area();
    }
    CHECK(sumArea == doctest::Approx(contentArea).epsilon(0.0001));
}

TEST_CASE("header treemap: children lie strictly inside the parent's padded content rect") {
    size_t nestedFrames = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        DirEntryStore store;
        auto cursor = store.allocateAppendCursor();
        RandomTree tree(seed);
        const EntryRef root = tree.build(store, cursor, 4);

        HeaderTreeMap map;
        map.build(store, root, makeOptions(800.0f + static_cast<float>(seed), 500.0f + static_cast<float>(seed * 3)));
        REQUIRE_FALSE(map.nodes().empty());

        for (uint32_t parent = 0; parent < map.nodes().size(); ++parent) {
            const HeaderTreeMapNode &parentNode = map.nodes()[parent];
            const std::vector<uint32_t> children = childIndices(map, parent);
            if (!children.empty()) {
                if (parent != 0) ++nestedFrames;
                REQUIRE(parentNode.frame == HeaderTreeMapFrame::Children);
                // Content sits below the header and inset by the padding.
                CHECK(parentNode.contentRect.y >= parentNode.headerRect.bottom() + kPadding - kEpsilon);
                CHECK(parentNode.contentRect.x >= parentNode.rect.x + kPadding - kEpsilon);
                CHECK(parentNode.contentRect.right() <= parentNode.rect.right() - kPadding + kEpsilon);
                CHECK(parentNode.contentRect.bottom() <= parentNode.rect.bottom() - kPadding + kEpsilon);
            }

            for (size_t i = 0; i < children.size(); ++i) {
                const HeaderTreeMapNode &child = map.nodes()[children[i]];
                CHECK(within(child.rect, parentNode.contentRect));
                CHECK(child.depth == parentNode.depth + 1);
                CHECK(children[i] > parent);
                for (size_t j = i + 1; j < children.size(); ++j)
                    CHECK(overlapArea(child.rect, map.nodes()[children[j]].rect) < kEpsilon);
            }

            if (parentNode.frame != HeaderTreeMapFrame::None) {
                CHECK(store[parentNode.ref].isDir());
                CHECK(parentNode.headerRect.height == kHeader);
            }
        }
    }
    // Make sure the invariants above were exercised on nested frames, not
    // just on the root.
    MESSAGE("nested directory frames checked: " << nestedFrames);
    CHECK(nestedFrames >= 40);
}

TEST_CASE("header treemap: entries below minNodeArea are culled") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 20001);
    const EntryRef big = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef medium = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef tiny = addTestEntry(store, cursor, EntryType::File, 1);
    linkChildren(store, root, {big, medium, tiny});

    HeaderTreeMap map;
    map.build(store, root, makeOptions(100.0f, 128.0f)); // tiny gets about 0.46 px^2

    CHECK(childIndices(map, 0).size() == 2);
    CHECK(findNode(map, tiny) == nullptr);
    CHECK(map.nodes()[0].childrenCulled);
}

TEST_CASE("header treemap: a hard-linked file below minNodeArea does not hide later siblings") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 19100);
    // Scanner order is by raw size; layout sizes are 10000, 1 and 100.
    const EntryRef big = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef linked = addTestEntry(store, cursor, EntryType::File, 9000);
    store[linked].hardLinks = 9000;
    const EntryRef later = addTestEntry(store, cursor, EntryType::File, 100);
    linkChildren(store, root, {big, linked, later});

    HeaderTreeMap map;
    map.build(store, root, makeOptions(100.0f + 2.0f * kPadding, 100.0f + kHeader + 2.0f * kPadding));

    CHECK(findNode(map, big) != nullptr);
    CHECK(findNode(map, linked) == nullptr);  // ~1 px^2
    REQUIRE(findNode(map, later) != nullptr); // ~99 px^2
    CHECK(findNode(map, later)->rect.area() >= 4.0f);
    CHECK(map.nodes()[0].childrenCulled);
}

TEST_CASE("header treemap: small directories degrade to header-only, then to a plain box") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 1000);
    const EntryRef bigFile = addTestEntry(store, cursor, EntryType::File, 700);
    // In a 300 x 30 content strip squarify lays out columns: bigFile 210 x 30,
    // mediumDir 84 x 30 (room for a header, but its content would be 2 px
    // tall) and tinyDir 6 x 30 (narrower than a header).
    const EntryRef mediumDir = addTestEntry(store, cursor, EntryType::Directory, 280);
    const EntryRef mediumChild = addTestEntry(store, cursor, EntryType::File, 280);
    const EntryRef tinyDir = addTestEntry(store, cursor, EntryType::Directory, 20);
    const EntryRef tinyChild = addTestEntry(store, cursor, EntryType::File, 20);
    linkChildren(store, root, {bigFile, mediumDir, tinyDir});
    linkChildren(store, mediumDir, {mediumChild});
    linkChildren(store, tinyDir, {tinyChild});

    HeaderTreeMapOptions options = makeOptions(300.0f + 2.0f * kPadding, 30.0f + kHeader + 2.0f * kPadding);
    HeaderTreeMap map;
    map.build(store, root, options);

    const HeaderTreeMapNode *medium = findNode(map, mediumDir);
    const HeaderTreeMapNode *tiny = findNode(map, tinyDir);
    REQUIRE(medium != nullptr);
    REQUIRE(tiny != nullptr);

    CHECK(medium->rect.width == doctest::Approx(84.0f));
    CHECK(medium->rect.height == doctest::Approx(30.0f));
    CHECK(medium->frame == HeaderTreeMapFrame::HeaderOnly);
    CHECK(medium->childrenCulled);
    CHECK(medium->firstChild == HeaderTreeMap::kNoNode);
    CHECK(medium->contentRect.empty());
    CHECK(medium->headerRect.height == kHeader);

    CHECK(tiny->rect.width < kHeader);
    CHECK(tiny->rect.width == doctest::Approx(6.0f));
    CHECK(tiny->frame == HeaderTreeMapFrame::None);
    CHECK(tiny->childrenCulled);
    CHECK(tiny->headerRect.empty());

    CHECK(findNode(map, mediumChild) == nullptr);
    CHECK(findNode(map, tinyChild) == nullptr);
}

TEST_CASE("header treemap: maxDepth keeps the header but drops children") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef level1 = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 100);
    linkChildren(store, root, {level1});
    linkChildren(store, level1, {file});

    HeaderTreeMapOptions options = makeOptions(400.0f, 400.0f);
    options.maxDepth = 1;
    HeaderTreeMap map;
    map.build(store, root, options);

    REQUIRE(map.nodes().size() == 2);
    CHECK(map.nodes()[1].frame == HeaderTreeMapFrame::HeaderOnly);
    CHECK(map.nodes()[1].childrenCulled);
}

TEST_CASE("header treemap: lookup in a header or padding gap returns the enclosing directory") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 200);
    const EntryRef subdir = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef inner = addTestEntry(store, cursor, EntryType::File, 100);
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 100);
    linkChildren(store, root, {subdir, file});
    linkChildren(store, subdir, {inner});

    HeaderTreeMap map;
    map.build(store, root, makeOptions(400.0f, 300.0f));

    const HeaderTreeMapNode &rootNode = map.nodes()[0];
    const HeaderTreeMapNode *subNode = findNode(map, subdir);
    const HeaderTreeMapNode *innerNode = findNode(map, inner);
    REQUIRE(subNode != nullptr);
    REQUIRE(innerNode != nullptr);
    REQUIRE(subNode->frame == HeaderTreeMapFrame::Children);

    // Root header and root padding.
    CHECK(map.lookup(200.0f, kHeader * 0.5f) == root);
    CHECK(map.lookup(kPadding * 0.5f, 150.0f) == root);
    CHECK(map.lookup(200.0f, rootNode.contentRect.y - kPadding * 0.5f) == root);
    CHECK(map.lookup(200.0f, 300.0f - kPadding * 0.5f) == root);

    // Subdirectory header and each side of its padding frame.
    const TreeMapRect &sub = subNode->rect;
    const TreeMapRect &content = subNode->contentRect;
    const float midX = sub.x + sub.width * 0.5f;
    const float midY = content.y + content.height * 0.5f;
    CHECK(map.lookup(midX, sub.y + kHeader * 0.5f) == subdir);
    CHECK(map.lookup(midX, content.y - kPadding * 0.5f) == subdir);
    CHECK(map.lookup(sub.x + kPadding * 0.5f, midY) == subdir);
    CHECK(map.lookup(content.right() + kPadding * 0.5f, midY) == subdir);
    CHECK(map.lookup(midX, content.bottom() + kPadding * 0.5f) == subdir);

    // Inside the frame the child wins.
    CHECK(map.lookup(midX, midY) == inner);
    CHECK(map.lookup(-1.0f, 10.0f) == ldirstat::kNoEntry);
}

TEST_CASE("header treemap: a focus file or empty input yields a sensible layout") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 42);
    const EntryRef emptyDir = addTestEntry(store, cursor, EntryType::Directory, 0);

    HeaderTreeMap map;
    map.build(store, file, makeOptions(50.0f, 50.0f));
    REQUIRE(map.nodes().size() == 1);
    CHECK(map.nodes()[0].frame == HeaderTreeMapFrame::None);
    CHECK(map.lookup(10.0f, 10.0f) == file);

    map.build(store, emptyDir, makeOptions(50.0f, 50.0f));
    CHECK(map.nodes().empty());
}
