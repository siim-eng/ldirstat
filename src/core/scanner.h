#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "direntrystore.h"
#include "namestore.h"

namespace ldirstat {

// Multi-threaded directory scanner using SYS_getdents64.
// Workers grab directories from a shared queue, enumerate entries,
// and push discovered subdirs back as a batch.
class Scanner {
public:
    Scanner(DirEntryStore& entry_store, NameStore& name_store);
    ~Scanner();

    // Blocks until scan completes or stop() is called.
    // Returns the EntryRef of the root directory.
    EntryRef scan(const std::string& root_path, int worker_count);

    // Signals all workers to stop. Can be called from any thread.
    void stop();

private:
    struct DirWork {
        std::string path;
        EntryRef ref;
    };

    struct WorkerCtx {
        std::vector<char> getdents_buf;
        std::vector<DirWork> subdir_batch;
        uint16_t entry_page;
        uint16_t name_page;
    };

    std::optional<DirWork> take_work();
    void return_work(std::vector<DirWork>& subdirs);
    void worker_loop(WorkerCtx& ctx);
    void scan_dir(const DirWork& work, WorkerCtx& ctx);

    DirEntryStore& entry_store_;
    NameStore& name_store_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<DirWork> queue_;
    int active_workers_ = 0;
    std::atomic<bool> stop_{false};

    std::vector<std::thread> threads_;
};

} // namespace ldirstat
