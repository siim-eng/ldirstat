#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace ldirstat {

enum class FileSystemType : uint8_t {
    Real,
    Network,
    Virtual,
    Temporary,
    Special,
    Unknown,
};

struct MountInfo {
    std::string device;
    std::string mountPoint;
    std::string fsType;
    dev_t dev;
    FileSystemType kind;
    uint64_t totalBytes;
    uint64_t availBytes;
};

// Reads /proc/mounts and provides mount point lookup.
class FileSystems {
public:
    // Parses /proc/mounts, stats each mount point for dev_t.
    void readMounts();

    // Returns the MountInfo for the mount point that contains the given device,
    // or nullptr if not found.
    const MountInfo* findByDevice(dev_t dev) const;

    const std::vector<MountInfo>& mounts() const { return mounts_; }

    static FileSystemType classifyFileSystem(std::string_view fsType);

private:
    std::vector<MountInfo> mounts_;
};

} // namespace ldirstat
