#pragma once

#include <cstddef>
#include <cstdint>

#include "namestore.h"

// Arena-friendly directory entry. Fixed-size, no heap allocations.
// Names live in a NameStore; entries live in a page-based DirEntryStore.

namespace ldirstat {

enum class FileCategory : uint16_t;
enum class FileType : uint16_t;

enum class EntryType : uint8_t {
    File,
    Directory,
    MountPoint,
    Symlink,
    Other,
};

// Reference to a DirEntry in the page-based store.
struct EntryRef {
    uint32_t pageId = UINT32_MAX;
    uint16_t index = UINT16_MAX;

    bool valid() const { return pageId != UINT32_MAX; }

    bool operator==(const EntryRef &other) const = default;
};

inline constexpr EntryRef kNoEntry{};

// A single node in the directory tree.
// Tree links use EntryRef (page_id + index) into the DirEntryStore.
// Name data is stored externally in a NameStore; NameRef locates it.
struct DirEntry {
    static constexpr uint8_t kCacheSubtreeFlag = 0x1;

    // Name (page_id + offset + length into NameStore).
    NameRef name;

    EntryType type = EntryType::File;
    uint8_t flags = 0;

    // Size in bytes. For files: st_size. For directories: sum of subtree.
    // Mount points are treated as empty directories and keep size 0.
    uint64_t size = 0;

    // Directory/file specific payload at the same offset:
    //  - directories and mount points use fileCount
    //  - regular files use fileType + hardLinks
    union {
        uint32_t fileCount = 0;
        struct {
            FileType fileType;
            uint16_t hardLinks;
        };
    };

    // Directory/file specific payload at the same offset:
    //  - directories and mount points use dirCount
    //  - regular files use packedModifiedMinutes (Unix epoch minutes)
    union {
        uint32_t dirCount = 0;
        uint32_t packedModifiedMinutes;
    };

    // Tree links (EntryRef into DirEntryStore).
    EntryRef parent;
    EntryRef firstChild;
    uint32_t childCount = 0; // number of direct children
    EntryRef nextSibling;

    bool isDir() const { return type == EntryType::Directory || type == EntryType::MountPoint; }
    bool isMountPoint() const { return type == EntryType::MountPoint; }
    bool isFile() const { return type == EntryType::File; }
    bool inCacheSubtree() const { return (flags & kCacheSubtreeFlag) != 0; }
    uint32_t modifiedMinutes() const { return packedModifiedMinutes; }
    void setCacheSubtree(bool enabled) {
        if (enabled) {
            flags |= kCacheSubtreeFlag;
            return;
        }
        flags &= static_cast<uint8_t>(~kCacheSubtreeFlag);
    }
    void setModifiedMinutes(uint32_t minutes) { packedModifiedMinutes = minutes; }
};

inline uint64_t layoutSizeOf(const DirEntry &entry) {
    if (!entry.isFile() || entry.hardLinks <= 1) return entry.size;

    return entry.size / static_cast<uint64_t>(entry.hardLinks);
}

static_assert(sizeof(EntryRef) == 8);
static_assert(offsetof(DirEntry, fileCount) == offsetof(DirEntry, fileType));
static_assert(offsetof(DirEntry, hardLinks) == offsetof(DirEntry, fileType) + sizeof(FileType));
static_assert(offsetof(DirEntry, dirCount) == offsetof(DirEntry, packedModifiedMinutes));
static_assert(sizeof(DirEntry) == 64);

} // namespace ldirstat
