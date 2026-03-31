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
    Scanner(DirEntryStore &entryStore, NameStore &nameStore);
    ~Scanner();

    // Blocks until scan completes or stop() is called.
    // Returns the EntryRef of the root directory.
    EntryRef scan(const std::string &rootPath, int workerCount);
    bool continueScan(EntryRef root, int workerCount);
    void commitContinueScan(EntryRef root);
    void revertContinueScan(EntryRef root);

    // Signals all workers to stop. Can be called from any thread.
    void stop();

    bool stopped() const { return stop_.load(std::memory_order_relaxed); }
    uint64_t filesScanned() const { return filesScanned_.load(std::memory_order_relaxed); }
    uint64_t dirsScanned() const { return dirsScanned_.load(std::memory_order_relaxed); }

    // Single-threaded post-scan pass. Propagates fileCount, dirCount,
    // size from child directories up to their parents.
    void propagate(EntryRef root, bool includeAncestors = true);

    // Sort each directory's children by size (descending).
    // Workers each take one pre-assigned slice from the current-pass tree snapshot.
    void sortBySize(int workerCount);

private:
    struct SortEntry {
        uint64_t size;
        EntryRef ref;
    };

    struct WorkerCtx {
        std::vector<char> getdentsBuf;
        std::vector<EntryRef> subdirBatch;
        std::vector<char> pathBuf;
        DirEntryStore::AppendCursor entryCursor;
        NameStore::AppendCursor nameCursor;
    };

    std::optional<EntryRef> takeWork();
    void returnWork(const std::vector<EntryRef> &subdirs);
    void workerLoop(WorkerCtx &ctx);
    void scanDir(EntryRef dirRef, WorkerCtx &ctx);
    void buildPath(EntryRef ref, std::vector<char> &pathBuf);
    void resetRuntimeState();
    void runScanWorkers(int workerCount,
                        std::vector<DirEntryStore::AppendCursor> entrySeeds = {},
                        std::vector<NameStore::AppendCursor> nameSeeds = {});
    void sortDirectoryChildren(EntryRef dirRef, std::vector<SortEntry> &scratch);

    DirEntryStore &entryStore_;
    NameStore &nameStore_;

    dev_t rootDev_ = 0;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<EntryRef> dirQueue_;
    size_t dirQueueNext_ = 0;
    int activeWorkers_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> filesScanned_{0};
    std::atomic<uint64_t> dirsScanned_{0};

    std::vector<std::thread> threads_;
};

} // namespace ldirstat
