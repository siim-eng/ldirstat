#include "scanner.h"

#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ldirstat {

namespace {

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

constexpr size_t kGetdentsBufSize = 32768;

bool isDotOrDotdot(const char* name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

} // namespace

Scanner::Scanner(DirEntryStore& entryStore, NameStore& nameStore)
    : entryStore_(entryStore)
    , nameStore_(nameStore) {}

Scanner::~Scanner() {
    stop();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

EntryRef Scanner::scan(const std::string& rootPath, int workerCount) {
    assert(workerCount > 0);
    stop_.store(false, std::memory_order_relaxed);

    // Reset state from any previous scan.
    dirQueue_.clear();
    dirQueueNext_ = 0;
    activeWorkers_ = 0;
    filesScanned_.store(0, std::memory_order_relaxed);
    dirsScanned_.store(0, std::memory_order_relaxed);

    // Stat root to get its device.
    struct stat rootSt{};
    if (stat(rootPath.c_str(), &rootSt) != 0)
        return kNoEntry;
    rootDev_ = rootSt.st_dev;

    // Create root entry.
    uint16_t entryPage = entryStore_.allocatePage();
    uint16_t namePage = nameStore_.allocatePage();
    EntryRef rootRef = entryStore_.add(entryPage);
    DirEntry& root = entryStore_[rootRef];
    root.type = EntryType::Directory;
    root.name = nameStore_.add(namePage, rootPath);
    root.depth = 0;
    root.device = rootSt.st_dev;

    {
        std::lock_guard lock(mutex_);
        dirQueue_.push_back(rootRef);
    }

    threads_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        threads_.emplace_back([this] {
            WorkerCtx ctx;
            ctx.getdentsBuf.resize(kGetdentsBufSize);
            ctx.pathBuf.resize(4096);
            ctx.entryPage = entryStore_.allocatePage();
            ctx.namePage = nameStore_.allocatePage();
            workerLoop(ctx);
        });
    }

    for (auto& t : threads_)
        t.join();
    threads_.clear();

    return rootRef;
}

void Scanner::stop() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}

void Scanner::propagate(EntryRef root) {
    std::vector<EntryRef> stack;
    stack.push_back(root);
    EntryRef child = entryStore_[root].firstChild;
    bool dirPopped = false;
    while (!stack.empty() && child.valid()) {
        DirEntry& entry = entryStore_[child];
        if (entry.isDir() && entry.dirCount > 0 && !dirPopped) {
            stack.push_back(child);
            child = entry.firstChild;
            continue;
        }

        if (entry.isDir() && entry.parent.valid()) {
            DirEntry& parent = entryStore_[entry.parent];
            parent.fileCount += entry.fileCount;
            parent.dirCount += entry.dirCount;
            parent.size += entry.size;
            parent.blocks += entry.blocks;
        }
        dirPopped = false;
        child = entry.nextSibling;
        if (!child.valid()) {
            child = stack.back();
            stack.pop_back();
            dirPopped = true;
        }
    }
}

std::optional<EntryRef> Scanner::takeWork() {
    std::unique_lock lock(mutex_);
    while (true) {
        if (stop_.load(std::memory_order_relaxed))
            return std::nullopt;
        if (dirQueueNext_ < dirQueue_.size()) {
            EntryRef ref = dirQueue_[dirQueueNext_++];
            ++activeWorkers_;
            return ref;
        }
        if (activeWorkers_ == 0)
            return std::nullopt; // all done
        cv_.wait(lock);
    }
}

void Scanner::returnWork(std::vector<EntryRef>& subdirs) {
    std::lock_guard lock(mutex_);
    for (auto ref : subdirs)
        dirQueue_.push_back(ref);
    --activeWorkers_;
    cv_.notify_all();
}

void Scanner::buildPath(EntryRef ref, WorkerCtx& ctx) {
    // Walk parent chain, collecting refs.
    EntryRef chain[256];
    int depth = 0;

    EntryRef cur = ref;
    while (cur.valid() && depth < 256) {
        chain[depth++] = cur;
        cur = entryStore_[cur].parent;
    }

    // Build path from root (chain[depth-1]) to target (chain[0]).
    // Root's name is the full root path; children are just filenames.
    size_t pos = 0;

    for (int i = depth - 1; i >= 0; --i) {
        std::string_view name = nameStore_.get(entryStore_[chain[i]].name);

        size_t needed = pos + 1 + name.size() + 1;
        if (needed > ctx.pathBuf.size())
            ctx.pathBuf.resize(needed * 2);

        if (i < depth - 1)
            ctx.pathBuf[pos++] = '/';

        std::memcpy(ctx.pathBuf.data() + pos, name.data(), name.size());
        pos += name.size();
    }

    ctx.pathBuf[pos] = '\0';
}

void Scanner::workerLoop(WorkerCtx& ctx) {
    while (auto ref = takeWork()) {
        scanDir(*ref, ctx);
        returnWork(ctx.subdirBatch);
        ctx.subdirBatch.clear();
    }
}

void Scanner::scanDir(EntryRef dirRef, WorkerCtx& ctx) {
    buildPath(dirRef, ctx);
    int fd = open(ctx.pathBuf.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;

    DirEntry& parent = entryStore_[dirRef];
    EntryRef prevChild;
    uint32_t files = 0;
    uint32_t dirs = 0;
    uint64_t totalSize = 0;
    uint64_t totalBlocks = 0;

    for (;;) {
        if (stop_.load(std::memory_order_relaxed))
            break;

        long nread = syscall(SYS_getdents64, fd,
                             ctx.getdentsBuf.data(),
                             ctx.getdentsBuf.size());
        if (nread <= 0) break;

        for (long pos = 0; pos < nread;) {
            auto* d = reinterpret_cast<linux_dirent64*>(
                ctx.getdentsBuf.data() + pos);
            pos += d->d_reclen;

            if (isDotOrDotdot(d->d_name))
                continue;

            // Create entry.
            EntryRef ref = entryStore_.add(ctx.entryPage);
            DirEntry& entry = entryStore_[ref];
            entry.name = nameStore_.add(ctx.namePage, d->d_name);
            entry.parent = dirRef;
            entry.depth = parent.depth + 1;

            // Stat for size, blocks, device, inode.
            struct stat st{};
            bool haveStat =
                fstatat(fd, d->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0;

            if (haveStat) {
                entry.size = static_cast<uint64_t>(st.st_size);
                entry.blocks = static_cast<uint64_t>(st.st_blocks);
                entry.device = st.st_dev;
                entry.inode = st.st_ino;
            }

            // Determine type: prefer d_type, fall back to stat.
            switch (d->d_type) {
            case DT_DIR: entry.type = EntryType::Directory; break;
            case DT_REG: entry.type = EntryType::File;      break;
            case DT_LNK: entry.type = EntryType::Symlink;   break;
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

            // Accumulate counts and totals.
            if (entry.type == EntryType::File) {
                ++files;
            } else if (entry.type == EntryType::Directory) {
                ++dirs;
            }
            totalSize += entry.size;
            totalBlocks += entry.blocks;

            // Chain child into parent's child list.
            if (prevChild.valid())
                entryStore_[prevChild].nextSibling = ref;
            else
                parent.firstChild = ref;
            prevChild = ref;
            ++parent.childCount;

            // Queue subdirectories (same filesystem only).
            if (entry.type == EntryType::Directory &&
                entry.device == rootDev_) {
                ctx.subdirBatch.push_back(ref);
            }
        }
    }

    parent.fileCount = files;
    parent.dirCount = dirs;
    parent.size += totalSize;
    parent.blocks += totalBlocks;

    filesScanned_.fetch_add(files, std::memory_order_relaxed);
    dirsScanned_.fetch_add(1, std::memory_order_relaxed);

    close(fd);
}

} // namespace ldirstat
