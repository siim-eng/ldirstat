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

bool is_dot_or_dotdot(const char* name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

} // namespace

Scanner::Scanner(DirEntryStore& entry_store, NameStore& name_store)
    : entry_store_(entry_store)
    , name_store_(name_store) {}

Scanner::~Scanner() {
    stop();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

EntryRef Scanner::scan(const std::string& root_path, int worker_count) {
    assert(worker_count > 0);
    stop_.store(false, std::memory_order_relaxed);

    // Reset state from any previous scan.
    queue_.clear();
    active_workers_ = 0;

    // Create root entry.
    uint16_t entry_page = entry_store_.allocate_page();
    uint16_t name_page = name_store_.allocate_page();
    EntryRef root_ref = entry_store_.add(entry_page);
    DirEntry& root = entry_store_[root_ref];
    root.type = EntryType::Directory;
    root.name = name_store_.add(name_page, root_path);
    root.depth = 0;

    {
        std::lock_guard lock(mutex_);
        queue_.push_back({root_path, root_ref});
    }

    threads_.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) {
        threads_.emplace_back([this] {
            WorkerCtx ctx;
            ctx.getdents_buf.resize(kGetdentsBufSize);
            ctx.entry_page = entry_store_.allocate_page();
            ctx.name_page = name_store_.allocate_page();
            worker_loop(ctx);
        });
    }

    for (auto& t : threads_)
        t.join();
    threads_.clear();

    return root_ref;
}

void Scanner::stop() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}

std::optional<Scanner::DirWork> Scanner::take_work() {
    std::unique_lock lock(mutex_);
    while (true) {
        if (stop_.load(std::memory_order_relaxed))
            return std::nullopt;
        if (!queue_.empty()) {
            auto work = std::move(queue_.back());
            queue_.pop_back();
            ++active_workers_;
            return work;
        }
        if (active_workers_ == 0)
            return std::nullopt; // all done
        cv_.wait(lock);
    }
}

void Scanner::return_work(std::vector<DirWork>& subdirs) {
    std::lock_guard lock(mutex_);
    for (auto& d : subdirs)
        queue_.push_back(std::move(d));
    --active_workers_;
    cv_.notify_all();
}

void Scanner::worker_loop(WorkerCtx& ctx) {
    while (auto work = take_work()) {
        scan_dir(*work, ctx);
        return_work(ctx.subdir_batch);
        ctx.subdir_batch.clear();
    }
}

void Scanner::scan_dir(const DirWork& work, WorkerCtx& ctx) {
    int fd = open(work.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;

    DirEntry& parent = entry_store_[work.ref];
    EntryRef prev_child;

    for (;;) {
        if (stop_.load(std::memory_order_relaxed))
            break;

        long nread = syscall(SYS_getdents64, fd,
                             ctx.getdents_buf.data(),
                             ctx.getdents_buf.size());
        if (nread <= 0) break;

        for (long pos = 0; pos < nread;) {
            auto* d = reinterpret_cast<linux_dirent64*>(
                ctx.getdents_buf.data() + pos);
            pos += d->d_reclen;

            if (is_dot_or_dotdot(d->d_name))
                continue;

            // Create entry.
            EntryRef ref = entry_store_.add(ctx.entry_page);
            DirEntry& entry = entry_store_[ref];
            entry.name = name_store_.add(ctx.name_page, d->d_name);
            entry.parent = work.ref;
            entry.depth = parent.depth + 1;

            // Stat for size, blocks, device, inode.
            struct stat st{};
            bool have_stat =
                fstatat(fd, d->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0;

            if (have_stat) {
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
                if (!have_stat) {
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
            if (prev_child.valid())
                entry_store_[prev_child].next_sibling = ref;
            else
                parent.first_child = ref;
            prev_child = ref;
            ++parent.child_count;

            // Queue subdirectories.
            if (entry.type == EntryType::Directory) {
                ctx.subdir_batch.push_back({
                    work.path + '/' + d->d_name,
                    ref,
                });
            }
        }
    }

    close(fd);
}

} // namespace ldirstat
