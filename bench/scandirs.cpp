#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

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
    scanner.propagate(rootRef);
    auto t1 = std::chrono::steady_clock::now();

    const ldirstat::DirEntry& root = entryStore[rootRef];

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double ms = us / 1000.0;

    std::printf("dirs:      %u\n", root.dirCount);
    std::printf("files:     %u\n", root.fileCount);
    std::printf("disk_used: %lu bytes\n", root.blocks * 512);
    std::printf("time:      %.3f ms\n", ms);

    return 0;
}
