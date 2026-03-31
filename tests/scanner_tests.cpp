#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scanner.h"

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path;

    TempDir() {
        std::string templ = (fs::temp_directory_path() / "ldirstat-scanner-XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *created = ::mkdtemp(buffer.data());
        REQUIRE(created != nullptr);
        path = created;
    }

    ~TempDir() {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeFile(const fs::path &path, std::string_view contents) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    REQUIRE(fd >= 0);
    REQUIRE(::write(fd, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()));
    REQUIRE(::close(fd) == 0);
}

void collectDescendantSnapshot(const ldirstat::DirEntryStore &entries,
                               const ldirstat::NameStore &names,
                               ldirstat::EntryRef dirRef,
                               const std::string &prefix,
                               std::vector<std::string> &snapshot) {
    for (ldirstat::EntryRef child = entries[dirRef].firstChild; child.valid(); child = entries[child].nextSibling) {
        const ldirstat::DirEntry &entry = entries[child];
        const std::string name(names.get(entry.name));
        const std::string path = prefix.empty() ? name : prefix + "/" + name;
        snapshot.push_back(path + "|" + std::to_string(static_cast<int>(entry.type)) + "|" +
                           std::to_string(entry.size) + "|" + std::to_string(entry.childCount));
        if (entry.isDir()) collectDescendantSnapshot(entries, names, child, path, snapshot);
    }
}

std::vector<std::string> descendantSnapshot(const ldirstat::DirEntryStore &entries,
                                            const ldirstat::NameStore &names,
                                            ldirstat::EntryRef rootRef) {
    std::vector<std::string> snapshot;
    collectDescendantSnapshot(entries, names, rootRef, {}, snapshot);
    std::sort(snapshot.begin(), snapshot.end());
    return snapshot;
}

struct ContinueScanFixture {
    TempDir tempDir;
    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner{entryStore, nameStore};
    ldirstat::EntryRef rootRef;
    ldirstat::EntryRef mountRef;

    ContinueScanFixture(int extraReusablePages = 0) {
        REQUIRE(fs::create_directory(mountPath()));

        auto entryCursor = entryStore.allocateAppendCursor();
        auto nameCursor = nameStore.allocateAppendCursor();

        rootRef = entryStore.add(entryCursor);
        auto &root = entryStore[rootRef];
        root.type = ldirstat::EntryType::Directory;
        root.name = nameStore.add(nameCursor, tempDir.path.string());

        mountRef = entryStore.add(entryCursor);
        auto &mount = entryStore[mountRef];
        mount.type = ldirstat::EntryType::MountPoint;
        mount.parent = rootRef;
        mount.name = nameStore.add(nameCursor, "mnt");

        root.firstChild = mountRef;
        root.childCount = 1;

        for (int i = 0; i < extraReusablePages; ++i) {
            auto extraEntryCursor = entryStore.allocateAppendCursor();
            auto extraNameCursor = nameStore.allocateAppendCursor();

            const ldirstat::EntryRef dummyRef = entryStore.add(extraEntryCursor);
            auto &dummy = entryStore[dummyRef];
            dummy.type = ldirstat::EntryType::Other;
            dummy.name = nameStore.add(extraNameCursor, "seed" + std::to_string(i));
        }
    }

    fs::path mountPath() const { return tempDir.path / "mnt"; }
};

TEST_CASE("scanner reuses the root append pages for fresh scans") {
    TempDir tempDir;
    writeFile(tempDir.path / "alpha.txt", "alpha");
    writeFile(tempDir.path / "beta.txt", "beta");

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef rootRef = scanner.scan(tempDir.path.string(), 1);
    REQUIRE(rootRef.valid());
    scanner.propagate(rootRef);
    scanner.sortBySize(1);

    CHECK(entryStore.pageCount() == 1);
    CHECK(nameStore.pageCount() == 1);

    const auto &root = entryStore[rootRef];
    REQUIRE(root.firstChild.valid());
    CHECK(root.firstChild.pageId == rootRef.pageId);
    CHECK(entryStore[root.firstChild].name.pageId == entryStore[rootRef].name.pageId);
    CHECK(root.childCount == 2);
}

TEST_CASE("continueScan seeds workers from existing reusable pages") {
    ContinueScanFixture fixture(2);
    writeFile(fixture.mountPath() / "alpha.txt", "alpha");
    writeFile(fixture.mountPath() / "beta.txt", "beta");
    REQUIRE(fs::create_directory(fixture.mountPath() / "nested"));
    writeFile(fixture.mountPath() / "nested" / "gamma.txt", "gamma");

    const uint32_t entryPagesBefore = fixture.entryStore.pageCount();
    const uint32_t namePagesBefore = fixture.nameStore.pageCount();

    REQUIRE(fixture.scanner.continueScan(fixture.mountRef, 3));

    CHECK(fixture.entryStore.pageCount() == entryPagesBefore);
    CHECK(fixture.nameStore.pageCount() == namePagesBefore);
}

TEST_CASE("continueScan matches a fresh scan for subtree results") {
    ContinueScanFixture fixture(2);
    writeFile(fixture.mountPath() / "alpha.txt", "alpha");
    writeFile(fixture.mountPath() / "beta.txt", "beta-beta");
    REQUIRE(fs::create_directory(fixture.mountPath() / "nested"));
    writeFile(fixture.mountPath() / "nested" / "gamma.txt", "gamma-gamma-gamma");

    REQUIRE(fixture.scanner.continueScan(fixture.mountRef, 3));

    ldirstat::DirEntryStore freshEntryStore;
    ldirstat::NameStore freshNameStore;
    ldirstat::Scanner freshScanner(freshEntryStore, freshNameStore);

    const ldirstat::EntryRef freshRoot = freshScanner.scan(fixture.mountPath().string(), 1);
    REQUIRE(freshRoot.valid());
    freshScanner.propagate(freshRoot);
    freshScanner.sortBySize(1);

    const auto &continuedRoot = fixture.entryStore[fixture.mountRef];
    const auto &freshRootEntry = freshEntryStore[freshRoot];

    CHECK(continuedRoot.size == freshRootEntry.size);
    CHECK(continuedRoot.fileCount == freshRootEntry.fileCount);
    CHECK(continuedRoot.dirCount == freshRootEntry.dirCount);
    CHECK(continuedRoot.childCount == freshRootEntry.childCount);
    CHECK(descendantSnapshot(fixture.entryStore, fixture.nameStore, fixture.mountRef) ==
          descendantSnapshot(freshEntryStore, freshNameStore, freshRoot));
}

} // namespace
