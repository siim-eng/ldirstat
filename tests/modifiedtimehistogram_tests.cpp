#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "modifiedtimehistogram.h"

#include <string_view>

namespace {

struct HistogramFixture {
    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::DirEntryStore::AppendCursor entryCursor = entryStore.allocateAppendCursor();
    ldirstat::NameStore::AppendCursor nameCursor = nameStore.allocateAppendCursor();

    ldirstat::EntryRef addDirectory(ldirstat::EntryRef parent, std::string_view name) {
        const ldirstat::EntryRef ref = entryStore.add(entryCursor);
        auto &entry = entryStore[ref];
        entry.type = ldirstat::EntryType::Directory;
        entry.name = nameStore.add(nameCursor, name);
        entry.parent = parent;

        if (parent.valid()) appendChild(parent, ref);
        return ref;
    }

    ldirstat::EntryRef addFile(ldirstat::EntryRef parent,
                               std::string_view name,
                               ldirstat::FileType fileType,
                               std::uint32_t modifiedMinutes,
                               std::uint64_t size) {
        const ldirstat::EntryRef ref = entryStore.add(entryCursor);
        auto &entry = entryStore[ref];
        entry.type = ldirstat::EntryType::File;
        entry.name = nameStore.add(nameCursor, name);
        entry.parent = parent;
        entry.fileType = fileType;
        entry.hardLinks = 1;
        entry.size = size;
        entry.setModifiedMinutes(modifiedMinutes);

        appendChild(parent, ref);
        return ref;
    }

    void appendChild(ldirstat::EntryRef parent, ldirstat::EntryRef child) {
        auto &parentEntry = entryStore[parent];
        if (!parentEntry.firstChild.valid()) {
            parentEntry.firstChild = child;
        } else {
            ldirstat::EntryRef last = parentEntry.firstChild;
            while (entryStore[last].nextSibling.valid())
                last = entryStore[last].nextSibling;
            entryStore[last].nextSibling = child;
        }
        ++parentEntry.childCount;
    }
};

} // namespace

TEST_CASE("modified time histogram reports bounds from known file timestamps") {
    HistogramFixture fixture;
    const ldirstat::EntryRef root = fixture.addDirectory(ldirstat::kNoEntry, "/root");
    fixture.addFile(root, "early.txt", ldirstat::FileType::ExtTxt, 100, 10);
    fixture.addFile(root, "late.txt", ldirstat::FileType::ExtTxt, 300, 20);
    fixture.addFile(root, "unknown.txt", ldirstat::FileType::ExtTxt, 0, 30);

    const ldirstat::ModifiedTimeHistogramBuilder builder(fixture.entryStore);
    const auto bounds = builder.bounds(root);

    CHECK(bounds.hasKnownFiles);
    CHECK(bounds.earliestMinutes == 100);
    CHECK(bounds.latestMinutes == 300);
}

TEST_CASE("modified time histogram bins counts and sizes by time and category") {
    HistogramFixture fixture;
    const ldirstat::EntryRef root = fixture.addDirectory(ldirstat::kNoEntry, "/root");
    fixture.addFile(root, "doc.txt", ldirstat::FileType::ExtTxt, 100, 10);
    fixture.addFile(root, "image.png", ldirstat::FileType::ExtPng, 112, 20);
    fixture.addFile(root, "cache.bin", ldirstat::FileType::Cache, 124, 40);
    fixture.addFile(root, "skip.txt", ldirstat::FileType::ExtTxt, 90, 100);
    fixture.addFile(root, "unknown.txt", ldirstat::FileType::ExtTxt, 0, 1000);

    const ldirstat::ModifiedTimeHistogramBuilder builder(fixture.entryStore);
    const auto bins = builder.build(root, 100, 124);

    CHECK(bins[0].startMinutes == 100);
    CHECK(bins[0].fileCount == 1);
    CHECK(bins[0].totalSize == 10);
    CHECK(bins[0].categories[ldirstat::FileCategorizer::categoryIndex(ldirstat::FileCategory::Document)].count == 1);

    CHECK(bins[12].startMinutes == 112);
    CHECK(bins[12].fileCount == 1);
    CHECK(bins[12].totalSize == 20);
    CHECK(bins[12].categories[ldirstat::FileCategorizer::categoryIndex(ldirstat::FileCategory::Image)].count == 1);

    CHECK(bins[23].fileCount == 1);
    CHECK(bins[23].totalSize == 40);
    CHECK(bins[23].categories[ldirstat::FileCategorizer::categoryIndex(ldirstat::FileCategory::Cache)].count == 1);
}

TEST_CASE("modified time histogram excludes files outside selected bounds") {
    HistogramFixture fixture;
    const ldirstat::EntryRef root = fixture.addDirectory(ldirstat::kNoEntry, "/root");
    fixture.addFile(root, "early.txt", ldirstat::FileType::ExtTxt, 90, 10);
    fixture.addFile(root, "inside-a.txt", ldirstat::FileType::ExtPng, 100, 20);
    fixture.addFile(root, "inside-b.txt", ldirstat::FileType::Cache, 120, 40);
    fixture.addFile(root, "late.txt", ldirstat::FileType::ExtTxt, 130, 80);

    const ldirstat::ModifiedTimeHistogramBuilder builder(fixture.entryStore);
    const auto bins = builder.build(root, 100, 120);

    std::uint64_t totalCount = 0;
    std::uint64_t totalSize = 0;
    for (const auto &bin : bins) {
        totalCount += bin.fileCount;
        totalSize += bin.totalSize;
    }

    CHECK(totalCount == 2);
    CHECK(totalSize == 60);
}

TEST_CASE("modified time histogram treats zero-span ranges as final-bin-only") {
    HistogramFixture fixture;
    const ldirstat::EntryRef root = fixture.addDirectory(ldirstat::kNoEntry, "/root");
    fixture.addFile(root, "same.txt", ldirstat::FileType::ExtTxt, 200, 64);

    const ldirstat::ModifiedTimeHistogramBuilder builder(fixture.entryStore);
    const auto bins = builder.build(root, 200, 200);

    CHECK(bins[23].fileCount == 1);
    CHECK(bins[23].totalSize == 64);
    CHECK(bins[0].fileCount == 0);
}
