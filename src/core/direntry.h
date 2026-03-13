#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include "namestore.h"

// Arena-friendly directory entry. Fixed-size, no heap allocations.
// Names live in a NameStore; children are contiguous in the entry arena.

namespace ldirstat {

// Sentinel for "no entry".
inline constexpr uint32_t kNoEntry = UINT32_MAX;

enum class EntryType : uint8_t {
    File,
    Directory,
    Symlink,
    Other,
};

// A single node in the directory tree.
// All indices refer to positions in the flat DirEntry arena.
// Name data is stored externally in a NameStore; NameRef locates it.
struct DirEntry {
    // Name (page_id + offset + length into NameStore).
    NameRef name;

    EntryType type = EntryType::File;
    uint8_t   depth = 0;          // tree depth (0 = root)

    // Size in bytes. For files: st_size. For directories: sum of subtree.
    uint64_t size = 0;

    // Allocated disk blocks (512-byte units from st_blocks).
    uint64_t blocks = 0;

    // Number of entries in subtree (excluding self). 0 for files.
    uint32_t subtree_count = 0;

    // Tree links (arena indices).
    uint32_t parent      = kNoEntry;
    uint32_t first_child = kNoEntry;  // first child index in arena
    uint32_t child_count = 0;         // number of direct children
    uint32_t next_sibling = kNoEntry; // next sibling in parent's child list

    // Device and inode for hardlink detection.
    dev_t device = 0;
    ino_t inode  = 0;

    bool is_dir()  const { return type == EntryType::Directory; }
    bool is_file() const { return type == EntryType::File; }
};

} // namespace ldirstat
