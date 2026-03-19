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

struct VolumeInfo {
    std::string devicePath;
    std::string mountPoint;
    std::string fsType;
    std::string label;
    std::string uuid;
    FileSystemType kind = FileSystemType::Unknown;
    uint64_t sizeBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t availBytes = 0;
    bool mounted = false;
    bool removable = false;
    bool hotplug = false;
    bool readOnly = false;
};

// Reads mounted filesystems and a unified welcome-screen volume list.
class FileSystems {
public:
    void refresh();

    // Compatibility wrapper for existing callers.
    void readMounts();

    // Returns the MountInfo for the mount point that contains the given device,
    // or nullptr if not found.
    const MountInfo* findByDevice(dev_t dev) const;

    const VolumeInfo* findVolumeByDevice(std::string_view devicePath) const;

    const std::vector<MountInfo>& mounts() const { return mounts_; }
    const std::vector<VolumeInfo>& volumes() const { return volumes_; }

    static FileSystemType classifyFileSystem(std::string_view fsType);

private:
    std::vector<MountInfo> mounts_;
    std::vector<VolumeInfo> volumes_;
};

} // namespace ldirstat
