#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <sys/types.h>

#include "direntrystore.h"
#include "dirworkqueue.h"
#include "namestore.h"

namespace ldirstat {

// Multi-threaded directory scanner using SYS_getdents64.
// Workers take directories from a shared DirWorkQueue, enumerate entries,
// and push discovered subdirs back as a batch. Worker threads are started
// and joined inside each scan(), continueScan() and sortBySize() call.
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
        uint64_t size = 0;
        EntryRef ref;
    };

    // Per-worker scratch state, reused for every directory the worker scans.
    struct WorkerCtx {
        WorkerCtx(DirEntryStore::AppendCursor entries, NameStore::AppendCursor names);

        std::vector<char> getdentsBuf;
        std::vector<EntryRef> subdirBatch;
        std::vector<char> pathBuf;
        DirEntryStore::AppendCursor entryCursor;
        NameStore::AppendCursor nameCursor;
    };

    void runScanWorkers(EntryRef root,
                        int workerCount,
                        std::vector<DirEntryStore::AppendCursor> entryCursors,
                        std::vector<NameStore::AppendCursor> nameCursors);
    void workerLoop(WorkerCtx &ctx);
    void scanDir(EntryRef dirRef, WorkerCtx &ctx);
    void buildPath(EntryRef ref, std::vector<char> &pathBuf);
    void resetRuntimeState();
    void sortDirectoryChildren(EntryRef dirRef, std::vector<SortEntry> &scratch);

    DirEntryStore &entryStore_;
    NameStore &nameStore_;

    dev_t rootDev_ = 0;
    EntryRef scanRoot_ = kNoEntry;

    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> filesScanned_{0};
    std::atomic<uint64_t> dirsScanned_{0};

    DirWorkQueue queue_{stop_};
};

} // namespace ldirstat
