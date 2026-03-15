#include <chrono>
#include <cstdio>

#include "filesystem.h"

int main() {
    ldirstat::FileSystems fs;

    auto t0 = std::chrono::steady_clock::now();
    fs.readMounts();
    auto t1 = std::chrono::steady_clock::now();

    for (const auto& m : fs.mounts()) {
        const char* kindStr = "";
        switch (m.kind) {
        case ldirstat::FileSystemType::Real:      kindStr = "real";      break;
        case ldirstat::FileSystemType::Network:   kindStr = "network";   break;
        case ldirstat::FileSystemType::Virtual:    kindStr = "virtual";   break;
        case ldirstat::FileSystemType::Temporary:  kindStr = "temporary"; break;
        case ldirstat::FileSystemType::Special:    kindStr = "special";   break;
        case ldirstat::FileSystemType::Unknown:    kindStr = "unknown";   break;
        }
        if (m.totalBytes > 0) {
            double totalGb = m.totalBytes / 1e9;
            double availGb = m.availBytes / 1e9;
            std::printf("%-10s %-20s %7.1fG %7.1fG  %s\n",
                        kindStr, m.fsType.c_str(), totalGb, availGb,
                        m.mountPoint.c_str());
        } else {
            std::printf("%-10s %-20s %7s %7s  %s\n",
                        kindStr, m.fsType.c_str(), "-", "-",
                        m.mountPoint.c_str());
        }
    }

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double ms = us / 1000.0;
    std::printf("\nmounts: %zu\n", fs.mounts().size());
    std::printf("time:   %.3f ms\n", ms);

    return 0;
}
