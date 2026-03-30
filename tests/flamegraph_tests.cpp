#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdint>
#include <initializer_list>

#include "flamegraph.h"

namespace {

ldirstat::EntryRef addTestEntry(ldirstat::DirEntryStore& entries,
                                std::uint32_t& entryPage,
                                ldirstat::EntryType type,
                                std::uint64_t size) {
    const ldirstat::EntryRef ref = entries.add(entryPage);
    auto& entry = entries[ref];
    entry.type = type;
    entry.size = size;
    entry.hardLinks = 1;
    return ref;
}

void linkChildren(ldirstat::DirEntryStore& entries,
                  ldirstat::EntryRef parentRef,
                  std::initializer_list<ldirstat::EntryRef> children) {
    auto& parent = entries[parentRef];
    parent.firstChild = ldirstat::kNoEntry;
    parent.childCount = static_cast<std::uint32_t>(children.size());

    ldirstat::EntryRef previous = ldirstat::kNoEntry;
    for (const ldirstat::EntryRef childRef : children) {
        auto& child = entries[childRef];
        child.parent = parentRef;
        child.nextSibling = ldirstat::kNoEntry;

        if (!parent.firstChild.valid())
            parent.firstChild = childRef;
        else
            entries[previous].nextSibling = childRef;

        previous = childRef;
    }
}

TEST_CASE("flamegraph emits all siblings above minimum pixel width") {
    ldirstat::DirEntryStore entryStore;
    std::uint32_t entryPage = entryStore.allocatePage();

    const ldirstat::EntryRef rootRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef firstRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 60);
    const ldirstat::EntryRef secondRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 25);
    const ldirstat::EntryRef thirdRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 15);
    linkChildren(entryStore, rootRef, {firstRef, secondRef, thirdRef});

    ldirstat::FlameGraph flameGraph;
    ldirstat::FlameGraphOptions options;
    options.width = 100.0f;
    options.minNodeWidth = 10.0f;
    flameGraph.build(entryStore, rootRef, options);

    REQUIRE(flameGraph.rowCount() == 2);
    REQUIRE(flameGraph.row(1).size() == 3);
    CHECK(flameGraph.row(1)[0].ref == firstRef);
    CHECK(flameGraph.row(1)[1].ref == secondRef);
    CHECK(flameGraph.row(1)[2].ref == thirdRef);
}

TEST_CASE("flamegraph stops scanning siblings once width cutoff is reached") {
    ldirstat::DirEntryStore entryStore;
    std::uint32_t entryPage = entryStore.allocatePage();

    const ldirstat::EntryRef rootRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef firstRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 70);
    const ldirstat::EntryRef secondRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 20);
    const ldirstat::EntryRef culledRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 5);
    const ldirstat::EntryRef laterRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 5);
    linkChildren(entryStore, rootRef, {firstRef, secondRef, culledRef, laterRef});

    ldirstat::FlameGraph flameGraph;
    ldirstat::FlameGraphOptions options;
    options.width = 100.0f;
    options.minNodeWidth = 10.0f;
    flameGraph.build(entryStore, rootRef, options);

    REQUIRE(flameGraph.rowCount() == 2);
    REQUIRE(flameGraph.row(1).size() == 2);
    CHECK(flameGraph.row(1)[0].ref == firstRef);
    CHECK(flameGraph.row(1)[1].ref == secondRef);
}

TEST_CASE("flamegraph skips zero-sized siblings before applying width cutoff") {
    ldirstat::DirEntryStore entryStore;
    std::uint32_t entryPage = entryStore.allocatePage();

    const ldirstat::EntryRef rootRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef firstRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 60);
    const ldirstat::EntryRef zeroRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 0);
    const ldirstat::EntryRef secondRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 30);
    const ldirstat::EntryRef culledRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 3);
    linkChildren(entryStore, rootRef, {firstRef, zeroRef, secondRef, culledRef});

    ldirstat::FlameGraph flameGraph;
    ldirstat::FlameGraphOptions options;
    options.width = 100.0f;
    options.minNodeWidth = 10.0f;
    flameGraph.build(entryStore, rootRef, options);

    REQUIRE(flameGraph.rowCount() == 2);
    REQUIRE(flameGraph.row(1).size() == 2);
    CHECK(flameGraph.row(1)[0].ref == firstRef);
    CHECK(flameGraph.row(1)[1].ref == secondRef);
}

TEST_CASE("flamegraph respects configured max depth") {
    ldirstat::DirEntryStore entryStore;
    std::uint32_t entryPage = entryStore.allocatePage();

    const ldirstat::EntryRef rootRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef dirARef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef dirBRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef fileRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 100);
    linkChildren(entryStore, rootRef, {dirARef});
    linkChildren(entryStore, dirARef, {dirBRef});
    linkChildren(entryStore, dirBRef, {fileRef});

    ldirstat::FlameGraph flameGraph;
    ldirstat::FlameGraphOptions options;
    options.width = 100.0f;
    options.minNodeWidth = 1.0f;
    options.maxDepth = 2;
    flameGraph.build(entryStore, rootRef, options);

    REQUIRE(flameGraph.rowCount() == 3);
    REQUIRE(flameGraph.row(1).size() == 1);
    REQUIRE(flameGraph.row(2).size() == 1);
    CHECK(flameGraph.row(1)[0].ref == dirARef);
    CHECK(flameGraph.row(2)[0].ref == dirBRef);
}

TEST_CASE("flamegraph keeps ancestry rows even when child widths are culled") {
    ldirstat::DirEntryStore entryStore;
    std::uint32_t entryPage = entryStore.allocatePage();

    const ldirstat::EntryRef rootRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef focusRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::Directory, 100);
    const ldirstat::EntryRef childRef =
        addTestEntry(entryStore, entryPage, ldirstat::EntryType::File, 50);
    linkChildren(entryStore, rootRef, {focusRef});
    linkChildren(entryStore, focusRef, {childRef});

    ldirstat::FlameGraph flameGraph;
    ldirstat::FlameGraphOptions options;
    options.width = 100.0f;
    options.minNodeWidth = 200.0f;
    flameGraph.build(entryStore, focusRef, options);

    REQUIRE(flameGraph.rowCount() == 2);
    REQUIRE(flameGraph.row(0).size() == 1);
    REQUIRE(flameGraph.row(1).size() == 1);
    CHECK(flameGraph.row(0)[0].ref == rootRef);
    CHECK(flameGraph.row(1)[0].ref == focusRef);
    CHECK(flameGraph.row(0)[0].x1 == doctest::Approx(0.0f));
    CHECK(flameGraph.row(0)[0].x2 == doctest::Approx(1.0f));
    CHECK(flameGraph.row(1)[0].x1 == doctest::Approx(0.0f));
    CHECK(flameGraph.row(1)[0].x2 == doctest::Approx(1.0f));
}

} // namespace
