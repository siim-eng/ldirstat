#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "direntry.h"

namespace ldirstat {

// Page-based arena for DirEntry nodes. Each page holds 65536 entries.
// Grows by adding pages — never reallocates existing pages.
// Workers get their own page to write to; page allocation is thread-safe,
// writing within a page is not.
class DirEntryStore {
public:
    static constexpr uint32_t kEntriesPerPage = 65536;

    // Thread-safe. Allocates a new empty page, returns its ID.
    uint16_t allocate_page() {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(pages_.size() < UINT16_MAX);
        auto id = static_cast<uint16_t>(pages_.size());
        pages_.push_back(std::make_unique<Page>());
        return id;
    }

    // NOT thread-safe per page. Caller must have exclusive access to current_page.
    // Returns an EntryRef to the claimed entry. If the page is full, allocates
    // a new page and updates current_page.
    EntryRef add(uint16_t& current_page) {
        assert(current_page < pages_.size());
        auto* page = pages_[current_page].get();

        if (page->used >= kEntriesPerPage) {
            current_page = allocate_page();
            page = pages_[current_page].get();
        }

        EntryRef ref{current_page, static_cast<uint16_t>(page->used)};
        ++page->used;
        return ref;
    }

    DirEntry& operator[](EntryRef ref) {
        return pages_[ref.page_id]->entries[ref.index];
    }

    const DirEntry& operator[](EntryRef ref) const {
        return pages_[ref.page_id]->entries[ref.index];
    }

    uint16_t page_count() const { return static_cast<uint16_t>(pages_.size()); }
    uint32_t page_used(uint16_t page_id) const { return pages_[page_id]->used; }

private:
    struct Page {
        std::array<DirEntry, kEntriesPerPage> entries{};
        uint32_t used = 0;
    };

    std::vector<std::unique_ptr<Page>> pages_;
    std::mutex mutex_;
};

} // namespace ldirstat
