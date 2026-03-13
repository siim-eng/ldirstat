#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include "namestore.h"

// Arena-friendly directory entry. Fixed-size, no heap allocations.
// Names live in a NameStore; entries live in a page-based DirEntryStore.

namespace ldirstat {

enum class EntryType : uint8_t {
    File,
    Directory,
    Symlink,
    Other,
};

// Reference to a DirEntry in the page-based store.
struct EntryRef {
    uint16_t pageId = UINT16_MAX;
    uint16_t index  = UINT16_MAX;

    bool valid() const { return pageId != UINT16_MAX; }
};

inline constexpr EntryRef kNoEntry{};

// A single node in the directory tree.
// Tree links use EntryRef (page_id + index) into the DirEntryStore.
// Name data is stored externally in a NameStore; NameRef locates it.
struct DirEntry {
    // Name (page_id + offset + length into NameStore).
    NameRef name;

    EntryType type = EntryType::File;
    uint16_t  depth = 0;          // tree depth (0 = root)

    // Size in bytes. For files: st_size. For directories: sum of subtree.
    uint64_t size = 0;

    // Allocated disk blocks (512-byte units from st_blocks).
    uint64_t blocks = 0;

    // Number of entries in subtree (excluding self). 0 for files.
    uint32_t subtreeCount = 0;

    // Tree links (EntryRef into DirEntryStore).
    EntryRef parent;
    EntryRef firstChild;
    uint32_t childCount = 0;         // number of direct children
    EntryRef nextSibling;

    // Device and inode for hardlink detection.
    dev_t device = 0;
    ino_t inode  = 0;

    bool isDir()  const { return type == EntryType::Directory; }
    bool isFile() const { return type == EntryType::File; }
};

} // namespace ldirstat
