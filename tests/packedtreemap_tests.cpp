#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "packedtreemap.h"

namespace {

using ldirstat::DirEntryStore;
using ldirstat::EntryRef;
using ldirstat::EntryType;
using ldirstat::PackedTreeMap;
using ldirstat::PackedTreeMapNode;
using ldirstat::PackedTreeMapOptions;
using ldirstat::TreeMapRect;

constexpr float kEpsilon = 0.01f;

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

std::vector<uint32_t> childIndices(const PackedTreeMap &map, uint32_t parent) {
    std::vector<uint32_t> result;
    for (uint32_t child = map.nodes()[parent].firstChild; child != PackedTreeMap::kNoNode; child = map.nodes()[child].nextSibling)
        result.push_back(child);
    return result;
}

const PackedTreeMapNode *findNode(const PackedTreeMap &map, EntryRef ref) {
    for (const PackedTreeMapNode &node : map.nodes()) {
        if (node.ref == ref) return &node;
    }
    return nullptr;
}

PackedTreeMapOptions squareOptions(float side) {
    PackedTreeMapOptions options;
    options.width = side;
    options.height = side;
    return options;
}

} // namespace

TEST_CASE("packed treemap: child areas are proportional to layout sizes and fill the parent") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 1000);
    const EntryRef a = addTestEntry(store, cursor, EntryType::File, 500);
    const EntryRef b = addTestEntry(store, cursor, EntryType::File, 250);
    const EntryRef c = addTestEntry(store, cursor, EntryType::File, 150);
    // Two hard links: counts half its size for layout purposes.
    const EntryRef d = addTestEntry(store, cursor, EntryType::File, 200);
    store[d].hardLinks = 2;
    linkChildren(store, root, {a, b, c, d});

    PackedTreeMap map;
    map.build(store, root, squareOptions(200.0f));

    const std::vector<uint32_t> children = childIndices(map, 0);
    REQUIRE(children.size() == 4);

    const double totalArea = 200.0 * 200.0;
    const double totalSize = 500.0 + 250.0 + 150.0 + 100.0;
    double sumArea = 0.0;
    for (const uint32_t index : children) {
        const PackedTreeMapNode &node = map.nodes()[index];
        const double expected = static_cast<double>(ldirstat::layoutSizeOf(store[node.ref])) / totalSize * totalArea;
        CHECK(node.rect.area() == doctest::Approx(expected).epsilon(0.001));
        sumArea += node.rect.area();
    }
    CHECK(sumArea == doctest::Approx(totalArea).epsilon(0.0001));
    CHECK_FALSE(map.nodes()[0].childrenCulled);
}

TEST_CASE("packed treemap: siblings never overlap and children stay inside their parent") {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        DirEntryStore store;
        auto cursor = store.allocateAppendCursor();
        RandomTree tree(seed);
        const EntryRef root = tree.build(store, cursor, 4);

        PackedTreeMapOptions options;
        options.width = 640.0f + static_cast<float>(seed);
        options.height = 360.0f + static_cast<float>(seed * 3);
        PackedTreeMap map;
        map.build(store, root, options);
        REQUIRE_FALSE(map.nodes().empty());

        for (uint32_t parent = 0; parent < map.nodes().size(); ++parent) {
            const std::vector<uint32_t> children = childIndices(map, parent);
            for (size_t i = 0; i < children.size(); ++i) {
                const PackedTreeMapNode &child = map.nodes()[children[i]];
                CHECK(std::min(child.rect.width, child.rect.height) >= options.minNodeSide - kEpsilon);
                CHECK(within(child.rect, map.nodes()[parent].rect));
                CHECK(child.depth == map.nodes()[parent].depth + 1);
                CHECK(children[i] > parent);
                for (size_t j = i + 1; j < children.size(); ++j)
                    CHECK(overlapArea(child.rect, map.nodes()[children[j]].rect) < kEpsilon);
            }
        }
    }
}

TEST_CASE("packed treemap: entries below minNodeArea are culled") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 20001);
    const EntryRef big = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef medium = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef tiny = addTestEntry(store, cursor, EntryType::File, 1);
    linkChildren(store, root, {big, medium, tiny});

    PackedTreeMapOptions options = squareOptions(100.0f);
    options.minNodeArea = 4.0f; // tiny would get ~0.5 px^2
    PackedTreeMap map;
    map.build(store, root, options);

    CHECK(childIndices(map, 0).size() == 2);
    CHECK(findNode(map, tiny) == nullptr);
    CHECK(map.nodes()[0].childrenCulled);
}

TEST_CASE("packed treemap: masses of tiny files are culled instead of drawn as slivers") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 10000);
    std::vector<EntryRef> children{addTestEntry(store, cursor, EntryType::File, 5000)};
    // 1000 files of ~5 px^2 each: above minNodeArea, but only ~2 px wide.
    for (int i = 0; i < 1000; ++i)
        children.push_back(addTestEntry(store, cursor, EntryType::File, 5));
    linkChildren(store, root, children);

    PackedTreeMapOptions options = squareOptions(100.0f);
    options.minNodeArea = 4.0f;
    options.minNodeSide = 3.0f;
    PackedTreeMap map;
    map.build(store, root, options);

    REQUIRE(childIndices(map, 0).size() == 1);
    CHECK(map.nodes()[1].ref == children.front());
    CHECK(map.nodes()[0].childrenCulled);
    // The culled files' space belongs to (and labels) the directory.
    const TreeMapRect &free = map.nodes()[0].labelRect;
    REQUIRE(free.area() > 0.0f);
    CHECK(map.lookup(free.x + free.width * 0.5f, free.y + free.height * 0.5f) == root);

    // Without the side limit the same files are emitted as slivers.
    options.minNodeSide = 0.0f;
    map.build(store, root, options);
    CHECK(childIndices(map, 0).size() > 1);
}

TEST_CASE("packed treemap: a hard-linked file below minNodeArea does not hide later siblings") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 13900);
    // Scanner order is by raw size; layout sizes are 5000, 1 and 4000.
    const EntryRef big = addTestEntry(store, cursor, EntryType::File, 5000);
    const EntryRef linked = addTestEntry(store, cursor, EntryType::File, 4900);
    store[linked].hardLinks = 4900;
    const EntryRef later = addTestEntry(store, cursor, EntryType::File, 4000);
    linkChildren(store, root, {big, linked, later});

    PackedTreeMap map;
    map.build(store, root, squareOptions(100.0f));

    CHECK(findNode(map, big) != nullptr);
    CHECK(findNode(map, linked) == nullptr);  // ~1 px^2
    REQUIRE(findNode(map, later) != nullptr); // ~4444 px^2
    CHECK(findNode(map, later)->rect.area() >= 16.0f);
    CHECK(map.nodes()[0].childrenCulled);
}

TEST_CASE("packed treemap: zero-sized entries are skipped without culling the parent") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef a = addTestEntry(store, cursor, EntryType::File, 60);
    const EntryRef empty = addTestEntry(store, cursor, EntryType::File, 0);
    const EntryRef b = addTestEntry(store, cursor, EntryType::File, 40);
    const EntryRef trailing = addTestEntry(store, cursor, EntryType::File, 0);
    linkChildren(store, root, {a, empty, b, trailing});

    PackedTreeMap map;
    map.build(store, root, squareOptions(100.0f));

    CHECK(childIndices(map, 0).size() == 2);
    CHECK(findNode(map, empty) == nullptr);
    CHECK(findNode(map, trailing) == nullptr);
    CHECK_FALSE(map.nodes()[0].childrenCulled);
}

TEST_CASE("packed treemap: directories too small for children are not descended") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef bigFile = addTestEntry(store, cursor, EntryType::File, 95);
    const EntryRef narrowDir = addTestEntry(store, cursor, EntryType::Directory, 5);
    const EntryRef nested = addTestEntry(store, cursor, EntryType::File, 5);
    linkChildren(store, root, {bigFile, narrowDir});
    linkChildren(store, narrowDir, {nested});

    PackedTreeMapOptions options = squareOptions(100.0f);
    options.minDirSideForChildren = 10.0f; // narrowDir gets a 5 px wide strip
    options.minDirAreaForChildren = 96.0f;
    PackedTreeMap map;
    map.build(store, root, options);

    const PackedTreeMapNode *dirNode = findNode(map, narrowDir);
    REQUIRE(dirNode != nullptr);
    CHECK(dirNode->firstChild == PackedTreeMap::kNoNode);
    CHECK(dirNode->childrenCulled);
    CHECK(findNode(map, nested) == nullptr);
}

TEST_CASE("packed treemap: maxDepth limits descent") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef level1 = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef level2 = addTestEntry(store, cursor, EntryType::Directory, 100);
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 100);
    linkChildren(store, root, {level1});
    linkChildren(store, level1, {level2});
    linkChildren(store, level2, {file});

    PackedTreeMapOptions options = squareOptions(100.0f);
    options.maxDepth = 2;
    PackedTreeMap map;
    map.build(store, root, options);

    REQUIRE(map.nodes().size() == 3);
    CHECK(map.nodes()[2].ref == level2);
    CHECK(map.nodes()[2].childrenCulled);
}

TEST_CASE("packed treemap: lookup returns the deepest entry and the directory for culled space") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef root = addTestEntry(store, cursor, EntryType::Directory, 20001);
    const EntryRef dir = addTestEntry(store, cursor, EntryType::Directory, 10000);
    const EntryRef inner = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 10000);
    const EntryRef tiny = addTestEntry(store, cursor, EntryType::File, 1);
    linkChildren(store, root, {dir, file, tiny});
    linkChildren(store, dir, {inner});

    PackedTreeMap map;
    map.build(store, root, squareOptions(100.0f));

    const PackedTreeMapNode *innerNode = findNode(map, inner);
    const PackedTreeMapNode *fileNode = findNode(map, file);
    REQUIRE(innerNode != nullptr);
    REQUIRE(fileNode != nullptr);

    CHECK(map.lookup(innerNode->rect.x + 1.0f, innerNode->rect.y + 1.0f) == inner);
    CHECK(map.lookup(fileNode->rect.x + 1.0f, fileNode->rect.y + 1.0f) == file);
    CHECK(map.lookup(-1.0f, 5.0f) == ldirstat::kNoEntry);
    CHECK(map.lookup(100.0f, 5.0f) == ldirstat::kNoEntry);

    // `tiny` was culled; its sliver of space belongs to the root and is the
    // root's label rect.
    const TreeMapRect &free = map.nodes()[0].labelRect;
    REQUIRE(free.area() > 0.0f);
    CHECK(map.lookup(free.x + free.width * 0.5f, free.y + free.height * 0.5f) == root);
}

TEST_CASE("packed treemap: a focus file or empty input yields a sensible layout") {
    DirEntryStore store;
    auto cursor = store.allocateAppendCursor();
    const EntryRef file = addTestEntry(store, cursor, EntryType::File, 42);
    const EntryRef emptyDir = addTestEntry(store, cursor, EntryType::Directory, 0);

    PackedTreeMap map;
    map.build(store, file, squareOptions(50.0f));
    REQUIRE(map.nodes().size() == 1);
    CHECK(map.lookup(10.0f, 10.0f) == file);

    map.build(store, emptyDir, squareOptions(50.0f));
    CHECK(map.nodes().empty());

    map.build(store, file, squareOptions(0.0f));
    CHECK(map.nodes().empty());
}
