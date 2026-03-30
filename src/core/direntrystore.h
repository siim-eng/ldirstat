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
// writing within a page is not. Page lookup is protected by a shared mutex
// so the store can keep growing without a fixed page cap.
class DirEntryStore {
public:
    static constexpr uint32_t kEntriesPerPage = 32768;

    void clear() {
        std::unique_lock lock(mutex_);
        pages_.clear();
    }

    // Thread-safe. Allocates a new empty page, returns its ID.
    uint32_t allocatePage() {
        auto page = std::make_unique<Page>();
        std::unique_lock lock(mutex_);
        assert(pages_.size() < UINT32_MAX);
        auto id = static_cast<uint32_t>(pages_.size());
        pages_.push_back(std::move(page));
        return id;
    }

    // NOT thread-safe per page. Caller must have exclusive access to currentPage.
    // Returns an EntryRef to the claimed entry. If the page is full, allocates
    // a new page and updates currentPage.
    EntryRef add(uint32_t& currentPage) {
        auto* page = pageFor(currentPage);

        if (page->used >= kEntriesPerPage) {
            currentPage = allocatePage();
            page = pageFor(currentPage);
        }

        EntryRef ref{currentPage, static_cast<uint16_t>(page->used)};
        ++page->used;
        return ref;
    }

    DirEntry& operator[](EntryRef ref) {
        return pageFor(ref.pageId)->entries[ref.index];
    }

    const DirEntry& operator[](EntryRef ref) const {
        return pageFor(ref.pageId)->entries[ref.index];
    }

    // Unhook an entry from the tree and propagate size/count changes up.
    // The entry's storage is not freed (arena allocator), but it becomes
    // unreachable from the tree.
    void remove(EntryRef ref) {
        DirEntry& entry = (*this)[ref];
        EntryRef parentRef = entry.parent;
        if (!parentRef.valid())
            return;

        // Unlink from parent's child list.
        DirEntry& parent = (*this)[parentRef];
        if (parent.firstChild == ref) {
            parent.firstChild = entry.nextSibling;
        } else {
            EntryRef prev = parent.firstChild;
            while (prev.valid()) {
                DirEntry& prevEntry = (*this)[prev];
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
            DirEntry& a = (*this)[ancestor];
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

    uint32_t pageUsed(uint32_t pageId) const { return pageFor(pageId)->used; }

private:
    static constexpr std::size_t kPageAlignment = 64;

    struct alignas(kPageAlignment) Page {
        std::array<DirEntry, kEntriesPerPage> entries{};
        uint32_t used = 0;
    };

    Page* pageFor(uint32_t pageId) {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }

    const Page* pageFor(uint32_t pageId) const {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }

    std::vector<std::unique_ptr<Page>> pages_;
    mutable std::shared_mutex mutex_;
};

} // namespace ldirstat
