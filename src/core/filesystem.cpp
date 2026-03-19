#include "filesystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sys/stat.h>
#include <sys/statvfs.h>

namespace ldirstat {

namespace {

bool isOctalDigit(char ch) {
    return ch >= '0' && ch <= '7';
}

int hexDigitValue(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
    return -1;
}

std::string decodeProcMountField(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' &&
            i + 3 < input.size() &&
            isOctalDigit(input[i + 1]) &&
            isOctalDigit(input[i + 2]) &&
            isOctalDigit(input[i + 3])) {
            const int value = ((input[i + 1] - '0') << 6) |
                              ((input[i + 2] - '0') << 3) |
                              (input[i + 3] - '0');
            output.push_back(static_cast<char>(value));
            i += 3;
            continue;
        }

        output.push_back(input[i]);
    }

    return output;
}

std::string decodeLsblkValue(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\\') {
            output.push_back(input[i]);
            continue;
        }

        if (i + 3 < input.size() && input[i + 1] == 'x') {
            const int hi = hexDigitValue(input[i + 2]);
            const int lo = hexDigitValue(input[i + 3]);
            if (hi >= 0 && lo >= 0) {
                output.push_back(static_cast<char>((hi << 4) | lo));
                i += 3;
                continue;
            }
        }

        if (i + 1 >= input.size()) {
            output.push_back('\\');
            continue;
        }

        const char escape = input[++i];
        switch (escape) {
        case 'n':
            output.push_back('\n');
            break;
        case 't':
            output.push_back('\t');
            break;
        case '\\':
            output.push_back('\\');
            break;
        case '"':
            output.push_back('"');
            break;
        default:
            output.push_back(escape);
            break;
        }
    }

    return output;
}

bool betterMountPoint(std::string_view candidate, std::string_view current) {
    if (candidate.empty())
        return false;
    if (current.empty())
        return true;
    return candidate.size() < current.size();
}

uint64_t parseUint64(std::string_view input) {
    uint64_t value = 0;
    for (const char ch : input) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return 0;
        value = value * 10 + static_cast<uint64_t>(ch - '0');
    }
    return value;
}

std::vector<std::string> splitMountPoints(std::string_view mountPoints) {
    std::vector<std::string> result;

    size_t start = 0;
    while (start <= mountPoints.size()) {
        const size_t end = mountPoints.find('\n', start);
        const std::string_view item =
            end == std::string_view::npos
                ? mountPoints.substr(start)
                : mountPoints.substr(start, end - start);

        if (!item.empty() && item != "[SWAP]")
            result.emplace_back(item);

        if (end == std::string_view::npos)
            break;

        start = end + 1;
    }

    return result;
}

bool parseLsblkLine(const std::string& line,
                    std::unordered_map<std::string, std::string>& fields) {
    fields.clear();

    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            ++pos;
        if (pos == line.size())
            return true;

        const size_t keyStart = pos;
        while (pos < line.size() && line[pos] != '=')
            ++pos;
        if (pos == line.size() || keyStart == pos)
            return false;

        const std::string key = line.substr(keyStart, pos - keyStart);
        ++pos;

        if (pos == line.size() || line[pos] != '"')
            return false;
        ++pos;

        std::string rawValue;
        bool foundClosingQuote = false;
        while (pos < line.size()) {
            const char ch = line[pos++];
            if (ch == '"') {
                foundClosingQuote = true;
                break;
            }

            if (ch == '\\' && pos < line.size()) {
                rawValue.push_back(ch);
                rawValue.push_back(line[pos++]);
                continue;
            }

            rawValue.push_back(ch);
        }

        if (!foundClosingQuote)
            return false;

        fields.emplace(key, decodeLsblkValue(rawValue));
    }

    return true;
}

std::vector<MountInfo> readProcMounts() {
    std::vector<MountInfo> mounts;

    std::ifstream file("/proc/mounts");
    if (!file.is_open())
        return mounts;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string device;
        std::string mountPoint;
        std::string fsType;
        if (!(iss >> device >> mountPoint >> fsType))
            continue;

        device = decodeProcMountField(device);
        mountPoint = decodeProcMountField(mountPoint);
        fsType = decodeProcMountField(fsType);

        struct stat st{};
        if (stat(mountPoint.c_str(), &st) != 0)
            continue;

        MountInfo info;
        info.device = std::move(device);
        info.mountPoint = std::move(mountPoint);
        info.fsType = std::move(fsType);
        info.dev = st.st_dev;
        info.kind = FileSystems::classifyFileSystem(info.fsType);
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

        mounts.push_back(std::move(info));
    }

    return mounts;
}

const MountInfo* findBestMountedLocal(const std::vector<MountInfo>& mounts,
                                      const std::string& devicePath) {
    const MountInfo* best = nullptr;

    for (const auto& mount : mounts) {
        if (mount.kind != FileSystemType::Real || mount.device != devicePath)
            continue;

        if (!best || betterMountPoint(mount.mountPoint, best->mountPoint))
            best = &mount;
    }

    return best;
}

VolumeInfo volumeFromMount(const MountInfo& mount) {
    VolumeInfo volume;
    volume.devicePath = mount.device;
    volume.mountPoint = mount.mountPoint;
    volume.fsType = mount.fsType;
    volume.kind = mount.kind;
    volume.sizeBytes = mount.totalBytes;
    volume.totalBytes = mount.totalBytes;
    volume.availBytes = mount.availBytes;
    volume.mounted = true;
    return volume;
}

void appendMountedLocalFallbackVolumes(const std::vector<MountInfo>& mounts,
                                       std::vector<VolumeInfo>& volumes) {
    std::unordered_map<std::string, size_t> byDevice;

    for (const auto& mount : mounts) {
        if (mount.kind != FileSystemType::Real)
            continue;

        auto it = byDevice.find(mount.device);
        if (it == byDevice.end()) {
            const size_t index = volumes.size();
            byDevice.emplace(mount.device, index);
            volumes.push_back(volumeFromMount(mount));
            continue;
        }

        VolumeInfo& volume = volumes[it->second];
        if (betterMountPoint(mount.mountPoint, volume.mountPoint)) {
            volume.mountPoint = mount.mountPoint;
            volume.totalBytes = mount.totalBytes;
            volume.availBytes = mount.availBytes;
        }

        volume.sizeBytes = std::max(volume.sizeBytes, mount.totalBytes);
    }
}

void mergeLocalVolume(VolumeInfo& dst, const VolumeInfo& src) {
    if (!src.mountPoint.empty() && betterMountPoint(src.mountPoint, dst.mountPoint))
        dst.mountPoint = src.mountPoint;

    if (!src.fsType.empty())
        dst.fsType = src.fsType;
    if (!src.label.empty())
        dst.label = src.label;
    if (!src.uuid.empty())
        dst.uuid = src.uuid;

    dst.kind = src.kind;
    dst.sizeBytes = std::max(dst.sizeBytes, src.sizeBytes);
    if (src.totalBytes > 0)
        dst.totalBytes = src.totalBytes;
    if (src.availBytes > 0 || src.totalBytes > 0)
        dst.availBytes = src.availBytes;
    dst.mounted = dst.mounted || src.mounted;
    dst.removable = dst.removable || src.removable;
    dst.hotplug = dst.hotplug || src.hotplug;
    dst.readOnly = dst.readOnly || src.readOnly;
}

bool appendLsblkVolumes(const std::vector<MountInfo>& mounts,
                        std::vector<VolumeInfo>& volumes) {
    FILE* pipe = popen(
        "lsblk -P -b -o PATH,TYPE,FSTYPE,LABEL,UUID,MOUNTPOINTS,SIZE,RM,HOTPLUG,RO",
        "r");
    if (!pipe)
        return false;

    std::unordered_map<std::string, std::string> fields;
    std::unordered_map<std::string, size_t> byDevice;
    std::array<char, 4096> chunk{};
    std::string pendingLine;
    bool parseSucceeded = true;

    auto processLine = [&](const std::string& line) {
        if (line.empty())
            return;

        if (!parseLsblkLine(line, fields)) {
            parseSucceeded = false;
            return;
        }

        const auto pathIt = fields.find("PATH");
        const auto fsTypeIt = fields.find("FSTYPE");
        if (pathIt == fields.end() || fsTypeIt == fields.end())
            return;

        const std::string& devicePath = pathIt->second;
        const std::string& fsType = fsTypeIt->second;
        if (devicePath.empty() || fsType.empty())
            return;

        const FileSystemType kind = FileSystems::classifyFileSystem(fsType);
        if (kind != FileSystemType::Real)
            return;

        VolumeInfo volume;
        volume.devicePath = devicePath;
        volume.fsType = fsType;
        volume.kind = kind;
        volume.sizeBytes = parseUint64(fields["SIZE"]);
        volume.label = fields["LABEL"];
        volume.uuid = fields["UUID"];
        volume.removable = fields["RM"] == "1";
        volume.hotplug = fields["HOTPLUG"] == "1";
        volume.readOnly = fields["RO"] == "1";

        for (const std::string& mountPoint : splitMountPoints(fields["MOUNTPOINTS"])) {
            volume.mounted = true;
            if (betterMountPoint(mountPoint, volume.mountPoint))
                volume.mountPoint = mountPoint;
        }

        if (const MountInfo* mountedInfo = findBestMountedLocal(mounts, devicePath)) {
            volume.mounted = true;
            if (betterMountPoint(mountedInfo->mountPoint, volume.mountPoint))
                volume.mountPoint = mountedInfo->mountPoint;
            volume.totalBytes = mountedInfo->totalBytes;
            volume.availBytes = mountedInfo->availBytes;
        }

        auto it = byDevice.find(devicePath);
        if (it == byDevice.end()) {
            const size_t index = volumes.size();
            byDevice.emplace(devicePath, index);
            volumes.push_back(std::move(volume));
            return;
        }

        mergeLocalVolume(volumes[it->second], volume);
    };

    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) {
        pendingLine += chunk.data();
        if (!pendingLine.empty() && pendingLine.back() == '\n') {
            pendingLine.pop_back();
            processLine(pendingLine);
            pendingLine.clear();
        }
    }

    if (!pendingLine.empty())
        processLine(pendingLine);

    const int status = pclose(pipe);
    return parseSucceeded && status == 0;
}

} // namespace

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

void FileSystems::refresh() {
    mounts_ = readProcMounts();

    volumes_.clear();
    volumes_.reserve(mounts_.size() + 8);

    for (const auto& mount : mounts_) {
        if (mount.kind != FileSystemType::Network)
            continue;
        volumes_.push_back(volumeFromMount(mount));
    }

    const size_t localVolumeStart = volumes_.size();
    const bool lsblkOk = appendLsblkVolumes(mounts_, volumes_);
    const bool hasMountedLocal = std::any_of(
        mounts_.begin(), mounts_.end(), [](const MountInfo& mount) {
            return mount.kind == FileSystemType::Real;
        });

    if (!lsblkOk)
        volumes_.resize(localVolumeStart);

    if (!lsblkOk || (volumes_.size() == localVolumeStart && hasMountedLocal))
        appendMountedLocalFallbackVolumes(mounts_, volumes_);
}

void FileSystems::readMounts() {
    refresh();
}

const MountInfo* FileSystems::findByDevice(dev_t dev) const {
    for (const auto& mount : mounts_) {
        if (mount.dev == dev)
            return &mount;
    }
    return nullptr;
}

const VolumeInfo* FileSystems::findVolumeByDevice(std::string_view devicePath) const {
    for (const auto& volume : volumes_) {
        if (volume.devicePath == devicePath)
            return &volume;
    }
    return nullptr;
}

} // namespace ldirstat
