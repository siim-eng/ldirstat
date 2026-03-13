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
    scanner.scan(rootPath, workerCount);
    auto t1 = std::chrono::steady_clock::now();

    uint64_t dirCount = 0;
    uint64_t fileCount = 0;
    uint64_t diskUsed = 0;

    for (uint16_t p = 0; p < entryStore.pageCount(); ++p) {
        uint32_t used = entryStore.pageUsed(p);
        for (uint32_t i = 0; i < used; ++i) {
            ldirstat::EntryRef ref{p, static_cast<uint16_t>(i)};
            const ldirstat::DirEntry& e = entryStore[ref];
            if (e.isDir())
                ++dirCount;
            else if (e.isFile())
                ++fileCount;
            diskUsed += e.blocks * 512;
        }
    }

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double ms = us / 1000.0;

    std::printf("dirs:      %lu\n", dirCount);
    std::printf("files:     %lu\n", fileCount);
    std::printf("disk_used: %lu bytes\n", diskUsed);
    std::printf("time:      %.3f ms\n", ms);

    return 0;
}
