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
    queue_.clear();
    activeWorkers_ = 0;

    // Create root entry.
    uint16_t entryPage = entryStore_.allocatePage();
    uint16_t namePage = nameStore_.allocatePage();
    EntryRef rootRef = entryStore_.add(entryPage);
    DirEntry& root = entryStore_[rootRef];
    root.type = EntryType::Directory;
    root.name = nameStore_.add(namePage, rootPath);
    root.depth = 0;

    {
        std::lock_guard lock(mutex_);
        queue_.push_back({rootPath, rootRef});
    }

    threads_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        threads_.emplace_back([this] {
            WorkerCtx ctx;
            ctx.getdentsBuf.resize(kGetdentsBufSize);
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

std::optional<Scanner::DirWork> Scanner::takeWork() {
    std::unique_lock lock(mutex_);
    while (true) {
        if (stop_.load(std::memory_order_relaxed))
            return std::nullopt;
        if (!queue_.empty()) {
            auto work = std::move(queue_.back());
            queue_.pop_back();
            ++activeWorkers_;
            return work;
        }
        if (activeWorkers_ == 0)
            return std::nullopt; // all done
        cv_.wait(lock);
    }
}

void Scanner::returnWork(std::vector<DirWork>& subdirs) {
    std::lock_guard lock(mutex_);
    for (auto& d : subdirs)
        queue_.push_back(std::move(d));
    --activeWorkers_;
    cv_.notify_all();
}

void Scanner::workerLoop(WorkerCtx& ctx) {
    while (auto work = takeWork()) {
        scanDir(*work, ctx);
        returnWork(ctx.subdirBatch);
        ctx.subdirBatch.clear();
    }
}

void Scanner::scanDir(const DirWork& work, WorkerCtx& ctx) {
    int fd = open(work.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;

    DirEntry& parent = entryStore_[work.ref];
    EntryRef prevChild;

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
            entry.parent = work.ref;
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

            // Chain child into parent's child list.
            if (prevChild.valid())
                entryStore_[prevChild].nextSibling = ref;
            else
                parent.firstChild = ref;
            prevChild = ref;
            ++parent.childCount;

            // Queue subdirectories.
            if (entry.type == EntryType::Directory) {
                ctx.subdirBatch.push_back({
                    work.path + '/' + d->d_name,
                    ref,
                });
            }
        }
    }

    close(fd);
}

} // namespace ldirstat
