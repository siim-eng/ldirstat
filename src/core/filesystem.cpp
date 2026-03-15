#include "filesystem.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace ldirstat {

FileSystemType FileSystems::classifyFileSystem(std::string_view fsType) {
    if (fsType == "ext4" || fsType == "xfs" || fsType == "btrfs" ||
        fsType == "f2fs" || fsType == "zfs" || fsType == "vfat" ||
        fsType == "exfat" || fsType == "ntfs" || fsType == "ntfs3")
        return FileSystemType::Real;

    if (fsType == "nfs" || fsType == "nfs4" || fsType == "cifs" ||
        fsType == "smb3" || fsType == "sshfs")
        return FileSystemType::Network;

    if (fsType == "tmpfs" || fsType == "ramfs")
        return FileSystemType::Temporary;

    if (fsType == "proc" || fsType == "sysfs" || fsType == "devtmpfs" ||
        fsType == "devpts" || fsType == "cgroup" || fsType == "cgroup2" ||
        fsType == "securityfs" || fsType == "pstore" || fsType == "debugfs" ||
        fsType == "tracefs" || fsType == "configfs" || fsType == "fusectl" ||
        fsType == "mqueue" || fsType == "hugetlbfs" || fsType == "fuse.portal" ||
        fsType == "autofs" || fsType == "nsfs" || fsType == "binfmt_misc" ||
        fsType == "efivarfs" || fsType == "bpf" || fsType == "selinuxfs")
        return FileSystemType::Virtual;

    if (fsType == "fuse.gvfsd-fuse")
        return FileSystemType::Special;

    return FileSystemType::Unknown;
}

void FileSystems::readMounts() {
    mounts_.clear();

    std::ifstream file("/proc/mounts");
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string device, mountPoint, fsType;
        if (!(iss >> device >> mountPoint >> fsType))
            continue;

        struct stat st{};
        if (stat(mountPoint.c_str(), &st) != 0)
            continue;

        MountInfo info;
        info.mountPoint = std::move(mountPoint);
        info.fsType = std::move(fsType);
        info.dev = st.st_dev;
        info.kind = classifyFileSystem(info.fsType);
        info.totalBytes = 0;
        info.availBytes = 0;

        if (info.kind == FileSystemType::Real ||
            info.kind == FileSystemType::Network) {
            struct statvfs svfs{};
            if (statvfs(info.mountPoint.c_str(), &svfs) == 0) {
                info.totalBytes = static_cast<uint64_t>(svfs.f_blocks) * svfs.f_frsize;
                info.availBytes = static_cast<uint64_t>(svfs.f_bavail) * svfs.f_frsize;
            }
        }

        mounts_.push_back(std::move(info));
    }
}

const MountInfo* FileSystems::findByDevice(dev_t dev) const {
    for (const auto& m : mounts_) {
        if (m.dev == dev)
            return &m;
    }
    return nullptr;
}

} // namespace ldirstat
