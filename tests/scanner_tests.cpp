#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdint>
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

std::string sizedContents(size_t size, char fill) {
    return std::string(size, fill);
}

// Reference total for a single-filesystem fixture.
uint64_t referenceAllocatedBytes(const fs::path &path) {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) return 0;

    uint64_t bytes = static_cast<uint64_t>(st.st_blocks) * 512;
    if (S_ISREG(st.st_mode) && st.st_nlink > 1) bytes /= static_cast<uint64_t>(st.st_nlink);
    if (!S_ISDIR(st.st_mode)) return bytes;

    std::error_code ec;
    for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
        bytes += referenceAllocatedBytes(it->path());
    return bytes;
}

ldirstat::EntryRef findChild(const ldirstat::DirEntryStore &entries,
                             const ldirstat::NameStore &names,
                             ldirstat::EntryRef dirRef,
                             std::string_view name) {
    for (ldirstat::EntryRef child = entries[dirRef].firstChild; child.valid(); child = entries[child].nextSibling) {
        if (names.get(entries[child].name) == name) return child;
    }
    return ldirstat::kNoEntry;
}

constexpr off_t kSparseFileSize = off_t{8} * 1024 * 1024;

void buildSizeAccountingTree(const fs::path &root) {
    writeFile(root / "small.txt", "small");
    writeFile(root / "pair-a.bin", sizedContents(20000, 'p'));
    REQUIRE(fs::create_directory(root / "empty"));
    REQUIRE(fs::create_directory(root / "sub"));
    writeFile(root / "sub" / "medium.bin", sizedContents(70000, 'm'));
    REQUIRE(::link((root / "pair-a.bin").c_str(), (root / "sub" / "pair-b.bin").c_str()) == 0);
    REQUIRE(::symlink("medium.bin", (root / "sub" / "link").c_str()) == 0);
    REQUIRE(fs::create_directory(root / "sub" / "deeper"));
    writeFile(root / "sub" / "deeper" / "tiny.txt", "t");

    const fs::path sparsePath = root / "sub" / "deeper" / "sparse.bin";
    const int fd = ::open(sparsePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    REQUIRE(fd >= 0);
    REQUIRE(::ftruncate(fd, kSparseFileSize) == 0);
    REQUIRE(::pwrite(fd, "x", 1, kSparseFileSize / 2) == 1);
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
        snapshot.push_back(path + "|" + std::to_string(static_cast<int>(entry.type)) + "|" + std::to_string(entry.size) + "|"
                           + std::to_string(entry.childCount));
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

std::vector<ldirstat::EntryRef> childRefs(const ldirstat::DirEntryStore &entries, ldirstat::EntryRef dirRef) {
    std::vector<ldirstat::EntryRef> refs;
    for (ldirstat::EntryRef child = entries[dirRef].firstChild; child.valid(); child = entries[child].nextSibling)
        refs.push_back(child);
    return refs;
}

void checkChildrenSortedBySize(const ldirstat::DirEntryStore &entries, ldirstat::EntryRef dirRef) {
    uint64_t prevSize = UINT64_MAX;
    for (ldirstat::EntryRef child = entries[dirRef].firstChild; child.valid(); child = entries[child].nextSibling) {
        const uint64_t size = entries[child].size;
        CHECK(size <= prevSize);
        prevSize = size;
    }
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

TEST_CASE("scan counts every inode's allocated bytes exactly once") {
    TempDir tempDir;
    buildSizeAccountingTree(tempDir.path);

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef rootRef = scanner.scan(tempDir.path.string(), 3);
    REQUIRE(rootRef.valid());
    scanner.propagate(rootRef);
    scanner.sortBySize(3);

    const auto &root = entryStore[rootRef];
    CHECK(root.size == referenceAllocatedBytes(tempDir.path));
    CHECK(root.fileCount == 6);
    CHECK(root.dirCount == 3);

    const ldirstat::EntryRef subRef = findChild(entryStore, nameStore, rootRef, "sub");
    REQUIRE(subRef.valid());
    CHECK(entryStore[subRef].size == referenceAllocatedBytes(tempDir.path / "sub"));

    const ldirstat::EntryRef deeperRef = findChild(entryStore, nameStore, subRef, "deeper");
    REQUIRE(deeperRef.valid());
    CHECK(entryStore[deeperRef].size == referenceAllocatedBytes(tempDir.path / "sub" / "deeper"));

    const ldirstat::EntryRef emptyRef = findChild(entryStore, nameStore, rootRef, "empty");
    REQUIRE(emptyRef.valid());
    CHECK(entryStore[emptyRef].size == referenceAllocatedBytes(tempDir.path / "empty"));

    const ldirstat::EntryRef sparseRef = findChild(entryStore, nameStore, deeperRef, "sparse.bin");
    REQUIRE(sparseRef.valid());
    CHECK(entryStore[sparseRef].size == static_cast<uint64_t>(kSparseFileSize));
}

// Every stat in "locked" fails, so this checks that failed entries add 0 bytes. It does not
// reproduce the old stale-accumulator bug, which needed a successful stat followed by a failed
// one in the same directory; permissions can't fail only some children of one directory.
TEST_CASE("entries whose stat fails add no allocated bytes") {
    if (::geteuid() == 0) return; // root ignores the missing search permission

    TempDir tempDir;
    writeFile(tempDir.path / "visible.bin", sizedContents(50000, 'v'));
    const fs::path lockedPath = tempDir.path / "locked";
    REQUIRE(fs::create_directory(lockedPath));
    writeFile(lockedPath / "hidden.bin", sizedContents(50000, 'h'));
    writeFile(lockedPath / "hidden2.bin", sizedContents(50000, 'h'));

    // Read permission allows listing names. Missing search permission blocks statx().
    struct PermissionGuard {
        fs::path path;
        ~PermissionGuard() { ::chmod(path.c_str(), 0755); }
    } guard{lockedPath};
    REQUIRE(::chmod(lockedPath.c_str(), 0444) == 0);

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef rootRef = scanner.scan(tempDir.path.string(), 2);
    REQUIRE(rootRef.valid());
    scanner.propagate(rootRef);

    const ldirstat::EntryRef lockedRef = findChild(entryStore, nameStore, rootRef, "locked");
    REQUIRE(lockedRef.valid());
    CHECK(entryStore[lockedRef].childCount == 2);
    CHECK(entryStore[lockedRef].size == referenceAllocatedBytes(lockedPath));
    CHECK(entryStore[rootRef].size == referenceAllocatedBytes(tempDir.path));
}

TEST_CASE("sortBySize sorts all current-pass directories across evenly split worker slices") {
    TempDir tempDir;
    constexpr int dirCount = 25;
    constexpr int filesPerDir = 3;

    for (int i = 0; i < dirCount; ++i) {
        const fs::path dirPath = tempDir.path / ("dir-" + std::to_string(i));
        REQUIRE(fs::create_directory(dirPath));

        writeFile(dirPath / "tiny.bin", sizedContents(static_cast<size_t>(i + 1), 'a'));
        writeFile(dirPath / "huge.bin", sizedContents(static_cast<size_t>(200 + i), 'b'));
        writeFile(dirPath / "mid.bin", sizedContents(static_cast<size_t>(100 + i), 'c'));
    }

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef rootRef = scanner.scan(tempDir.path.string(), 4);
    REQUIRE(rootRef.valid());
    scanner.propagate(rootRef);
    scanner.sortBySize(4);

    const std::vector<ldirstat::EntryRef> rootChildren = childRefs(entryStore, rootRef);
    REQUIRE(rootChildren.size() == dirCount);
    checkChildrenSortedBySize(entryStore, rootRef);

    for (ldirstat::EntryRef dirRef : rootChildren) {
        CHECK(entryStore[dirRef].isDir());
        REQUIRE(entryStore[dirRef].childCount == filesPerDir);
        checkChildrenSortedBySize(entryStore, dirRef);
    }
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
    CHECK(descendantSnapshot(fixture.entryStore, fixture.nameStore, fixture.mountRef)
          == descendantSnapshot(freshEntryStore, freshNameStore, freshRoot));
}

TEST_CASE("continueScan counts the continued subtree once and commit adds it to ancestors") {
    ContinueScanFixture fixture;
    buildSizeAccountingTree(fixture.mountPath());
    const uint64_t expected = referenceAllocatedBytes(fixture.mountPath());

    REQUIRE(fixture.scanner.continueScan(fixture.mountRef, 2));
    CHECK(fixture.entryStore[fixture.mountRef].size == expected);

    fixture.scanner.revertContinueScan(fixture.mountRef);
    CHECK(fixture.entryStore[fixture.mountRef].size == 0);
    CHECK(fixture.entryStore[fixture.rootRef].size == 0);

    REQUIRE(fixture.scanner.continueScan(fixture.mountRef, 2));
    fixture.scanner.commitContinueScan(fixture.mountRef);
    CHECK(fixture.entryStore[fixture.mountRef].size == expected);
    CHECK(fixture.entryStore[fixture.rootRef].size == expected);
}

TEST_CASE("continueScan leaves ancestor order untouched until commitContinueScan resorts it") {
    ContinueScanFixture fixture;

    auto entryCursor = fixture.entryStore.allocateAppendCursor();
    auto nameCursor = fixture.nameStore.allocateAppendCursor();

    const ldirstat::EntryRef siblingRef = fixture.entryStore.add(entryCursor);
    auto &sibling = fixture.entryStore[siblingRef];
    sibling.type = ldirstat::EntryType::File;
    sibling.parent = fixture.rootRef;
    sibling.name = fixture.nameStore.add(nameCursor, "small.txt");
    sibling.size = 1;

    auto &root = fixture.entryStore[fixture.rootRef];
    root.firstChild = siblingRef;
    root.childCount = 2;
    root.fileCount = 1;
    root.size = 1;
    sibling.nextSibling = fixture.mountRef;

    writeFile(fixture.mountPath() / "large.bin", sizedContents(256, 'z'));

    REQUIRE(fixture.scanner.continueScan(fixture.mountRef, 3));

    CHECK(root.firstChild == siblingRef);
    CHECK(fixture.entryStore[siblingRef].nextSibling == fixture.mountRef);
    CHECK(root.size == 1);

    fixture.scanner.commitContinueScan(fixture.mountRef);

    CHECK(root.firstChild == fixture.mountRef);
    CHECK(fixture.entryStore[fixture.mountRef].nextSibling == siblingRef);
    CHECK(root.size > fixture.entryStore[siblingRef].size);
}

} // namespace
