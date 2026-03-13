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
    Scanner(DirEntryStore& entryStore, NameStore& nameStore);
    ~Scanner();

    // Blocks until scan completes or stop() is called.
    // Returns the EntryRef of the root directory.
    EntryRef scan(const std::string& rootPath, int workerCount);

    // Signals all workers to stop. Can be called from any thread.
    void stop();

    // Single-threaded post-scan pass. Propagates fileCount, dirCount,
    // size, and blocks from child directories up to their parents.
    void propagate(EntryRef root);

private:
    struct DirWork {
        std::string path;
        EntryRef ref;
    };

    struct WorkerCtx {
        std::vector<char> getdentsBuf;
        std::vector<DirWork> subdirBatch;
        uint16_t entryPage;
        uint16_t namePage;
    };

    std::optional<DirWork> takeWork();
    void returnWork(std::vector<DirWork>& subdirs);
    void workerLoop(WorkerCtx& ctx);
    void scanDir(const DirWork& work, WorkerCtx& ctx);

    DirEntryStore& entryStore_;
    NameStore& nameStore_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<DirWork> queue_;
    int activeWorkers_ = 0;
    std::atomic<bool> stop_{false};

    std::vector<std::thread> threads_;
};

} // namespace ldirstat
