#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "direntry.h"

namespace ldirstat {

// Page-based arena for DirEntry nodes. Each page holds 32768 entries.
// Grows by adding pages — never reallocates existing pages.
// Workers get their own page to write to; page allocation is thread-safe,
// writing within a page is not. Read-side page lookup is protected by a
// shared mutex so the store can keep growing without a fixed page cap.
class DirEntryStore {
public:
    static constexpr uint32_t kEntriesPerPage = 32768;

private:
    static constexpr std::size_t kPageAlignment = 64;

    struct alignas(kPageAlignment) Page {
        std::array<DirEntry, kEntriesPerPage> entries{};
        uint32_t used = 0;
    };

public:
    using PageHandle = Page;

    struct AppendCursor {
        uint32_t pageId = 0;
        PageHandle *page = nullptr;
    };

    void clear() {
        std::unique_lock lock(mutex_);
        pages_.clear();
    }

    // Thread-safe. Allocates a new empty page and returns an append cursor
    // that can be used without page lookup on the hot path.
    AppendCursor allocateAppendCursor() {
        auto page = std::make_unique<Page>();
        std::unique_lock lock(mutex_);
        assert(pages_.size() < UINT32_MAX);
        auto id = static_cast<uint32_t>(pages_.size());
        pages_.push_back(std::move(page));
        return {id, pages_.back().get()};
    }

    // Returns up to maxCount non-full pages as append cursors. Call this
    // before launching workers, while page allocation is still single-threaded.
    // Pages with 15% or less free space are skipped so startup seeding only
    // reuses pages that still have meaningful headroom.
    std::vector<AppendCursor> reusableAppendCursors(std::size_t maxCount) {
        std::vector<AppendCursor> cursors;
        cursors.reserve(maxCount);
        for (uint32_t i = 0; i < pages_.size(); ++i) {
            Page *page = pages_[i].get();
            if (page->used >= kEntriesPerPage) continue;
            const uint32_t freeEntries = kEntriesPerPage - page->used;
            if (static_cast<uint64_t>(freeEntries) * 100 <= static_cast<uint64_t>(kEntriesPerPage) * 5) continue;
            cursors.push_back({i, page});
            if (cursors.size() >= maxCount) break;
        }
        return cursors;
    }

    // NOT thread-safe per page. Caller must have exclusive access to cursor.page.
    // Returns an EntryRef to the claimed entry. If the page is full, allocates
    // a new page and updates the cursor.
    EntryRef add(AppendCursor &cursor) {
        Page *page = cursor.page;
        assert(page != nullptr);

        if (page->used >= kEntriesPerPage) {
            cursor = allocateAppendCursor();
            page = cursor.page;
        }

        EntryRef ref{cursor.pageId, static_cast<uint16_t>(page->used)};
        ++page->used;
        return ref;
    }

    // Unlocked lookup — use only when the store is not growing (post-scan).
    DirEntry &operator[](EntryRef ref) { return pageFor(ref.pageId)->entries[ref.index]; }
    const DirEntry &operator[](EntryRef ref) const { return pageFor(ref.pageId)->entries[ref.index]; }

    // Thread-safe lookup — safe to call while other threads may allocate pages.
    DirEntry &at(EntryRef ref) { return safePageFor(ref.pageId)->entries[ref.index]; }
    const DirEntry &at(EntryRef ref) const { return safePageFor(ref.pageId)->entries[ref.index]; }

    // Unhook an entry from the tree and propagate size/count changes up.
    // The entry's storage is not freed (arena allocator), but it becomes
    // unreachable from the tree.
    void remove(EntryRef ref) {
        DirEntry &entry = (*this)[ref];
        EntryRef parentRef = entry.parent;
        if (!parentRef.valid()) return;

        // Unlink from parent's child list.
        DirEntry &parent = (*this)[parentRef];
        if (parent.firstChild == ref) {
            parent.firstChild = entry.nextSibling;
        } else {
            EntryRef prev = parent.firstChild;
            while (prev.valid()) {
                DirEntry &prevEntry = (*this)[prev];
                if (prevEntry.nextSibling == ref) {
                    prevEntry.nextSibling = entry.nextSibling;
                    break;
                }
                prev = prevEntry.nextSibling;
            }
        }
        parent.childCount--;

        // Propagate size and count changes up to root.
        uint64_t removedSize = entry.size;
        uint32_t removedFiles = entry.isDir() ? entry.fileCount : 1;
        uint32_t removedDirs = entry.isDir() ? (entry.dirCount + 1) : 0;

        EntryRef ancestor = parentRef;
        while (ancestor.valid()) {
            DirEntry &a = (*this)[ancestor];
            a.size -= removedSize;
            a.fileCount -= removedFiles;
            a.dirCount -= removedDirs;
            ancestor = a.parent;
        }

        // Clear the removed entry's links.
        entry.parent = kNoEntry;
        entry.nextSibling = kNoEntry;
    }

    uint32_t pageCount() const {
        std::shared_lock lock(mutex_);
        return static_cast<uint32_t>(pages_.size());
    }

private:
    Page *pageFor(uint32_t pageId) { return pages_[pageId].get(); }
    const Page *pageFor(uint32_t pageId) const { return pages_[pageId].get(); }

    Page *safePageFor(uint32_t pageId) {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }
    const Page *safePageFor(uint32_t pageId) const {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }

    std::vector<std::unique_ptr<Page>> pages_;
    mutable std::shared_mutex mutex_;
};

} // namespace ldirstat
