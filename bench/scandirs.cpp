#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <fstream>

#include "direntrystore.h"
#include "namestore.h"
#include "scanner.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <rootdir> <worker_count>\n", argv[0]);
        return 1;
    }

    std::string rootPath = argv[1];
    int workerCount = std::atoi(argv[2]);
    if (workerCount < 1) {
        std::fprintf(stderr, "worker_count must be >= 1\n");
        return 1;
    }

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    auto t0 = std::chrono::steady_clock::now();
    ldirstat::EntryRef rootRef = scanner.scan(rootPath, workerCount);
    auto t1 = std::chrono::steady_clock::now();
    scanner.propagate(rootRef);
    auto t2 = std::chrono::steady_clock::now();
    scanner.sortBySize(workerCount);
    auto t3 = std::chrono::steady_clock::now();

    const ldirstat::DirEntry& root = entryStore[rootRef];

    double scanMs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    double propagateMs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
    double sortMs = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;

    // Memory usage.
    uint16_t entryPages = entryStore.pageCount();
    uint16_t namePages = nameStore.pageCount();
    size_t entryBytes = static_cast<size_t>(entryPages)
        * ldirstat::DirEntryStore::kEntriesPerPage * sizeof(ldirstat::DirEntry);
    size_t nameBytes = static_cast<size_t>(namePages) * 65536;
    size_t totalBytes = entryBytes + nameBytes;

    std::printf("dirs:      %u\n", root.dirCount);
    std::printf("files:     %u\n", root.fileCount);
    std::printf("disk_used: %lu bytes\n", root.size);
    std::printf("scan:      %.3f ms\n", scanMs);
    std::printf("propagate: %.3f ms\n", propagateMs);
    std::printf("sort:      %.3f ms\n", sortMs);
    std::printf("memory:    %.1f MB (entries: %u pages / %.1f MB, names: %u pages / %.1f MB)\n",
                totalBytes / (1024.0 * 1024.0),
                entryPages, entryBytes / (1024.0 * 1024.0),
                namePages, nameBytes / (1024.0 * 1024.0));

    // Read RSS from /proc/self/status.
    long rssKb = 0;
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::sscanf(line.c_str(), "VmRSS: %ld", &rssKb);
            break;
        }
    }
    std::printf("rss:       %.1f MB\n", rssKb / 1024.0);

    return 0;
}
