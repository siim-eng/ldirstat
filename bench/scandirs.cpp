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

    std::string root_path = argv[1];
    int worker_count = std::atoi(argv[2]);
    if (worker_count < 1) {
        std::fprintf(stderr, "worker_count must be >= 1\n");
        return 1;
    }

    ldirstat::DirEntryStore entry_store;
    ldirstat::NameStore name_store;
    ldirstat::Scanner scanner(entry_store, name_store);

    auto t0 = std::chrono::steady_clock::now();
    scanner.scan(root_path, worker_count);
    auto t1 = std::chrono::steady_clock::now();

    uint64_t dir_count = 0;
    uint64_t file_count = 0;
    uint64_t disk_used = 0;

    for (uint16_t p = 0; p < entry_store.page_count(); ++p) {
        uint32_t used = entry_store.page_used(p);
        for (uint32_t i = 0; i < used; ++i) {
            ldirstat::EntryRef ref{p, static_cast<uint16_t>(i)};
            const ldirstat::DirEntry& e = entry_store[ref];
            if (e.is_dir())
                ++dir_count;
            else if (e.is_file())
                ++file_count;
            disk_used += e.blocks * 512;
        }
    }

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double ms = us / 1000.0;

    std::printf("dirs:      %lu\n", dir_count);
    std::printf("files:     %lu\n", file_count);
    std::printf("disk_used: %lu bytes\n", disk_used);
    std::printf("time:      %.3f ms\n", ms);

    return 0;
}
