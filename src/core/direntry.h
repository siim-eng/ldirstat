#pragma once

#include <cstdint>

#include "namestore.h"

// Arena-friendly directory entry. Fixed-size, no heap allocations.
// Names live in a NameStore; entries live in a page-based DirEntryStore.

namespace ldirstat {

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
    uint16_t index  = UINT16_MAX;

    bool valid() const { return pageId != UINT32_MAX; }

    bool operator==(const EntryRef& other) const {
        return pageId == other.pageId && index == other.index;
    }
    bool operator!=(const EntryRef& other) const { return !(*this == other); }
};

inline constexpr EntryRef kNoEntry{};

// A single node in the directory tree.
// Tree links use EntryRef (page_id + index) into the DirEntryStore.
// Name data is stored externally in a NameStore; NameRef locates it.
struct DirEntry {
    // Name (page_id + offset + length into NameStore).
    NameRef name;

    EntryType type = EntryType::File;

    // Size in bytes. For files: st_size. For directories: sum of subtree.
    // Mount points are treated as empty directories and keep size 0.
    uint64_t size = 0;

    // Number of files/dirs in subtree (excluding self). 0 for files.
    uint32_t fileCount = 0;
    uint32_t dirCount = 0;

    // Tree links (EntryRef into DirEntryStore).
    EntryRef parent;
    EntryRef firstChild;
    uint32_t childCount = 0;         // number of direct children
    EntryRef nextSibling;

    bool isDir() const {
        return type == EntryType::Directory || type == EntryType::MountPoint;
    }
    bool isMountPoint() const { return type == EntryType::MountPoint; }
    bool isFile() const { return type == EntryType::File; }
};

static_assert(sizeof(EntryRef) == 8);
static_assert(sizeof(DirEntry) == 64);

} // namespace ldirstat
