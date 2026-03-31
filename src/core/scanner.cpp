#include "scanner.h"

#include "filecategorizer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ldirstat {

namespace {

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

constexpr size_t kGetdentsBufSize = 32768;
constexpr size_t kInitialPathBufSize = 4096;

bool isDotOrDotdot(const char *name) {
    return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

uint16_t clampHardLinks(nlink_t links) {
    constexpr nlink_t kMaxHardLinks = static_cast<nlink_t>(std::numeric_limits<uint16_t>::max());
    if (links > kMaxHardLinks) return std::numeric_limits<uint16_t>::max();
    return static_cast<uint16_t>(links);
}

bool isExecutableByMode(const struct stat &st) {
    return S_ISREG(st.st_mode) && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

template<typename Visitor> void traverseDirectoryTree(const DirEntryStore &entryStore, EntryRef rootRef, Visitor &&visit) {
    if (!rootRef.valid()) return;

    std::vector<EntryRef> stack;
    stack.push_back(rootRef);

    while (!stack.empty()) {
        const EntryRef dirRef = stack.back();
        stack.pop_back();

        const DirEntry &dir = entryStore[dirRef];
        if (!dir.isDir()) continue;

        visit(dirRef);

        for (EntryRef child = dir.firstChild; child.valid(); child = entryStore[child].nextSibling) {
            if (entryStore[child].isDir()) stack.push_back(child);
        }
    }
}

} // namespace

Scanner::Scanner(DirEntryStore &entryStore, NameStore &nameStore)
    : entryStore_(entryStore),
      nameStore_(nameStore) {}

Scanner::~Scanner() {
    stop();
    for (auto &t : threads_)
        if (t.joinable()) t.join();
}

EntryRef Scanner::scan(const std::string &rootPath, int workerCount) {
    assert(workerCount > 0);
    resetRuntimeState();

    // Stat root to get its device.
    struct stat rootSt{};
    if (stat(rootPath.c_str(), &rootSt) != 0) return kNoEntry;
    rootDev_ = rootSt.st_dev;

    // Create root entry.
    DirEntryStore::AppendCursor entryCursor = entryStore_.allocateAppendCursor();
    NameStore::AppendCursor nameCursor = nameStore_.allocateAppendCursor();
    EntryRef rootRef = entryStore_.add(entryCursor);
    DirEntry &root = entryStore_[rootRef];
    root.type = EntryType::Directory;
    root.name = nameStore_.add(nameCursor, rootPath);
    dirQueue_.push_back(rootRef);

    runScanWorkers(workerCount, {entryCursor}, {nameCursor});

    return rootRef;
}

bool Scanner::continueScan(EntryRef root, int workerCount) {
    assert(workerCount > 0);
    if (!root.valid()) return false;

    const DirEntry &existing = entryStore_[root];
    if (!existing.isMountPoint()) return false;

    resetRuntimeState();

    std::vector<char> pathBuf(kInitialPathBufSize);
    buildPath(root, pathBuf);

    struct stat rootSt{};
    if (stat(pathBuf.data(), &rootSt) != 0 || !S_ISDIR(rootSt.st_mode)) return false;

    int fd = open(pathBuf.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    close(fd);

    rootDev_ = rootSt.st_dev;

    DirEntry &rootEntry = entryStore_[root];
    rootEntry.type = EntryType::Directory;
    rootEntry.size = 0;
    rootEntry.fileCount = 0;
    rootEntry.dirCount = 0;
    rootEntry.firstChild = kNoEntry;
    rootEntry.childCount = 0;

    dirQueue_.push_back(root);

    runScanWorkers(workerCount,
                   entryStore_.reusableAppendCursors(static_cast<size_t>(workerCount)),
                   nameStore_.reusableAppendCursors(static_cast<size_t>(workerCount)));
    propagate(root, false);
    sortBySize(workerCount);
    return !stopped();
}

void Scanner::commitContinueScan(EntryRef root) {
    if (!root.valid()) return;

    DirEntry &rootEntry = entryStore_[root];
    rootEntry.type = EntryType::Directory;

    EntryRef ancestor = rootEntry.parent;
    while (ancestor.valid()) {
        DirEntry &entry = entryStore_[ancestor];
        entry.size += rootEntry.size;
        entry.fileCount += rootEntry.fileCount;
        entry.dirCount += rootEntry.dirCount;
        ancestor = entry.parent;
    }

    std::vector<SortEntry> scratch;
    for (EntryRef dir = rootEntry.parent; dir.valid(); dir = entryStore_[dir].parent)
        sortDirectoryChildren(dir, scratch);
}

void Scanner::revertContinueScan(EntryRef root) {
    if (!root.valid()) return;

    DirEntry &rootEntry = entryStore_[root];
    rootEntry.type = EntryType::MountPoint;
    rootEntry.size = 0;
    rootEntry.fileCount = 0;
    rootEntry.dirCount = 0;
    rootEntry.firstChild = kNoEntry;
    rootEntry.childCount = 0;
}

void Scanner::stop() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}

void Scanner::propagate(EntryRef root, bool includeAncestors) {
    if (!root.valid()) return;

    std::vector<EntryRef> stack;
    stack.push_back(root);
    EntryRef child = entryStore_[root].firstChild;
    bool dirPopped = false;
    while (!stack.empty() && child.valid()) {
        DirEntry &entry = entryStore_[child];
        if (entry.isDir() && entry.dirCount > 0 && !dirPopped) {
            stack.push_back(child);
            child = entry.firstChild;
            continue;
        }

        if (entry.isDir() && entry.parent.valid()) {
            DirEntry &parent = entryStore_[entry.parent];
            parent.fileCount += entry.fileCount;
            parent.dirCount += entry.dirCount;
            parent.size += entry.size;
        }
        dirPopped = false;
        child = entry.nextSibling;
        if (!child.valid()) {
            child = stack.back();
            stack.pop_back();
            dirPopped = true;
        }
    }

    if (!includeAncestors) return;

    const DirEntry &rootEntry = entryStore_[root];
    EntryRef ancestor = rootEntry.parent;
    while (ancestor.valid()) {
        DirEntry &entry = entryStore_[ancestor];
        entry.fileCount += rootEntry.fileCount;
        entry.dirCount += rootEntry.dirCount;
        entry.size += rootEntry.size;
        ancestor = entry.parent;
    }
}

std::optional<EntryRef> Scanner::takeWork() {
    std::unique_lock lock(mutex_);
    while (true) {
        if (stop_.load(std::memory_order_relaxed)) return std::nullopt;
        if (dirQueueNext_ < dirQueue_.size()) {
            EntryRef ref = dirQueue_[dirQueueNext_++];
            ++activeWorkers_;
            return ref;
        }
        if (activeWorkers_ == 0) return std::nullopt; // all done
        cv_.wait(lock);
    }
}

void Scanner::returnWork(const std::vector<EntryRef> &subdirs) {
    std::lock_guard lock(mutex_);
    dirQueue_.insert(dirQueue_.end(), subdirs.begin(), subdirs.end());
    --activeWorkers_;
    cv_.notify_all();
}

void Scanner::buildPath(EntryRef ref, std::vector<char> &pathBuf) {
    // avoid memory allocation per directory path creation, use scratch buffer
    // kind of useless optimization, but still worth around 2%-5%

    // Walk parent chain, collecting refs.
    EntryRef chain[256];
    int depth = 0;

    EntryRef cur = ref;
    while (cur.valid() && depth < 256) {
        chain[depth++] = cur;
        cur = entryStore_.at(cur).parent;
    }

    // Build path from root (chain[depth-1]) to target (chain[0]).
    // Root's name is the full root path; children are just filenames.
    size_t pos = 0;

    for (int i = depth - 1; i >= 0; --i) {
        std::string_view name = nameStore_.get(entryStore_.at(chain[i]).name);

        size_t needed = pos + 1 + name.size() + 1;
        if (needed > pathBuf.size()) pathBuf.resize(needed * 2);

        if (i < depth - 1) pathBuf[pos++] = '/';

        std::memcpy(pathBuf.data() + pos, name.data(), name.size());
        pos += name.size();
    }

    pathBuf[pos] = '\0';
}

void Scanner::resetRuntimeState() {
    stop_.store(false, std::memory_order_relaxed);
    filesScanned_.store(0, std::memory_order_relaxed);
    dirsScanned_.store(0, std::memory_order_relaxed);

    std::lock_guard lock(mutex_);
    dirQueue_.clear();
    dirQueueNext_ = 0;
    activeWorkers_ = 0;
}

void Scanner::runScanWorkers(int workerCount,
                             std::vector<DirEntryStore::AppendCursor> entrySeeds,
                             std::vector<NameStore::AppendCursor> nameSeeds) {
    threads_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        const DirEntryStore::AppendCursor entrySeed =
            i < static_cast<int>(entrySeeds.size()) ? entrySeeds[i] : DirEntryStore::AppendCursor{};
        const NameStore::AppendCursor nameSeed =
            i < static_cast<int>(nameSeeds.size()) ? nameSeeds[i] : NameStore::AppendCursor{};

        threads_.emplace_back([this, entrySeed, nameSeed] {
            WorkerCtx ctx;
            ctx.getdentsBuf.resize(kGetdentsBufSize);
            ctx.pathBuf.resize(kInitialPathBufSize);
            ctx.entryCursor = entrySeed.page != nullptr ? entrySeed : entryStore_.allocateAppendCursor();
            ctx.nameCursor = nameSeed.page != nullptr ? nameSeed : nameStore_.allocateAppendCursor();
            workerLoop(ctx);
        });
    }

    for (auto &t : threads_)
        t.join();
    threads_.clear();
}

void Scanner::workerLoop(WorkerCtx &ctx) {
    while (auto ref = takeWork()) {
        scanDir(*ref, ctx);
        returnWork(ctx.subdirBatch);
        ctx.subdirBatch.clear();
    }
}

void Scanner::scanDir(EntryRef dirRef, WorkerCtx &ctx) {
    buildPath(dirRef, ctx.pathBuf);
    int fd = open(ctx.pathBuf.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;

    DirEntry &parent = entryStore_.at(dirRef);
    EntryRef prevChild;
    uint32_t files = 0;
    uint32_t dirs = 0;
    uint64_t totalAllocatedBytes = 0;
    uint64_t allocatedBytes = 0;

    for (;;) {
        if (stop_.load(std::memory_order_relaxed)) break;

        // using syscall instead of readdir allows us to reuse buffer, readdir does malloc internally
        long nread = syscall(SYS_getdents64, fd, ctx.getdentsBuf.data(), ctx.getdentsBuf.size());
        if (nread <= 0) break;

        for (long pos = 0; pos < nread;) {
            auto *d = reinterpret_cast<linux_dirent64 *>(ctx.getdentsBuf.data() + pos);
            pos += d->d_reclen;

            if (isDotOrDotdot(d->d_name)) continue;

            // Create entry.
            EntryRef ref = entryStore_.add(ctx.entryCursor);
            DirEntry &entry = entryStore_.at(ref);
            entry.name = nameStore_.add(ctx.nameCursor, d->d_name);
            entry.parent = dirRef;

            // Stat for size, and same-filesystem filtering.
            struct stat st{};
            bool haveStat = fstatat(fd, d->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0;

            if (haveStat) {
                entry.size = static_cast<uint64_t>(st.st_size);

                allocatedBytes = static_cast<uint64_t>(st.st_blocks) * 512;
                if (S_ISREG(st.st_mode)) {
                    const uint64_t linkCount = static_cast<uint64_t>(st.st_nlink);
                    if (linkCount > 1) allocatedBytes /= linkCount;
                }
            }

            // Determine type: prefer d_type, fall back to stat.
            switch (d->d_type) {
            case DT_DIR: entry.type = EntryType::Directory; break;
            case DT_REG: entry.type = EntryType::File; break;
            case DT_LNK: entry.type = EntryType::Symlink; break;
            default:
                if (!haveStat) {
                    entry.type = EntryType::Other;
                } else if (S_ISDIR(st.st_mode)) {
                    entry.type = EntryType::Directory;
                } else if (S_ISREG(st.st_mode)) {
                    entry.type = EntryType::File;
                } else if (S_ISLNK(st.st_mode)) {
                    entry.type = EntryType::Symlink;
                } else {
                    entry.type = EntryType::Other;
                }
                break;
            }

            if (entry.type == EntryType::Directory && haveStat && st.st_dev != rootDev_) {
                entry.type = EntryType::MountPoint;
                entry.size = 0;
                allocatedBytes = 0;
            }

            if (entry.isFile()) {
                FileCategory category = FileCategorizer::categorize(d->d_name);
                if (category == FileCategory::Unknown && haveStat && isExecutableByMode(st)) category = FileCategory::Executable;
                entry.fileCategory = category;
                entry.hardLinks = haveStat ? clampHardLinks(st.st_nlink) : 0;
            }

            // Accumulate counts and totals.
            if (entry.isFile()) {
                ++files;
            } else if (entry.isDir()) {
                ++dirs;
            }
            totalAllocatedBytes += allocatedBytes;

            // Chain child into parent's child list.
            if (prevChild.valid())
                entryStore_.at(prevChild).nextSibling = ref;
            else
                parent.firstChild = ref;
            prevChild = ref;
            ++parent.childCount;

            // Queue subdirectories (same filesystem only).
            if (entry.type == EntryType::Directory && haveStat && st.st_dev == rootDev_) {
                ctx.subdirBatch.push_back(ref);
            }
        }
    }

    parent.fileCount = files;
    parent.dirCount = dirs;
    parent.size += totalAllocatedBytes;

    filesScanned_.fetch_add(files, std::memory_order_relaxed);
    dirsScanned_.fetch_add(1, std::memory_order_relaxed);

    close(fd);
}

void Scanner::sortDirectoryChildren(EntryRef dirRef, std::vector<SortEntry> &scratch) {
    DirEntry &dir = entryStore_[dirRef];
    if (dir.childCount < 2) return;

    scratch.clear();
    EntryRef child = dir.firstChild;
    while (child.valid()) {
        scratch.push_back({entryStore_[child].size, child});
        child = entryStore_[child].nextSibling;
    }

    std::sort(scratch.begin(), scratch.end(), [](const SortEntry &a, const SortEntry &b) { return a.size > b.size; });

    dir.firstChild = scratch[0].ref;
    for (size_t i = 0; i + 1 < scratch.size(); ++i)
        entryStore_[scratch[i].ref].nextSibling = scratch[i + 1].ref;
    entryStore_[scratch.back().ref].nextSibling = kNoEntry;
}

void Scanner::sortBySize(int workerCount) {
    EntryRef sortRoot;
    if (!dirQueue_.empty()) sortRoot = dirQueue_.front();
    if (!sortRoot.valid()) return;

    std::vector<EntryRef> sortTargets;
    sortTargets.reserve(static_cast<size_t>(entryStore_[sortRoot].dirCount) + 1);
    traverseDirectoryTree(entryStore_, sortRoot, [&sortTargets](EntryRef dirRef) { sortTargets.push_back(dirRef); });
    if (sortTargets.empty()) return;

    const size_t activeWorkers = std::min(static_cast<size_t>(workerCount), sortTargets.size());

    auto sortWorker = [this, &sortTargets](size_t start, size_t end) {
        std::vector<SortEntry> scratch;
        for (size_t i = start; i < end; ++i) {
            if (stop_.load(std::memory_order_relaxed)) return;
            sortDirectoryChildren(sortTargets[i], scratch);
        }
    };

    threads_.reserve(activeWorkers);
    for (size_t i = 0; i < activeWorkers; ++i) {
        const size_t start = sortTargets.size() * i / activeWorkers;
        const size_t end = sortTargets.size() * (i + 1) / activeWorkers;
        threads_.emplace_back(sortWorker, start, end);
    }

    for (auto &t : threads_)
        t.join();
    threads_.clear();
}

} // namespace ldirstat
