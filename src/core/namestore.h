#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <vector>

namespace ldirstat {

struct NameRef {
    uint32_t pageId = 0;
    uint16_t offset = 0;
    uint16_t length = 0;
};

static_assert(sizeof(NameRef) == 8);

// Page-based string storage. 64KB pages, no resizes — only new pages are added.
// Thread safety:
//   - allocateAppendCursor(): thread-safe (exclusive lock)
//   - add():                  NOT thread-safe per page — caller must own the page exclusively
//   - get():           thread-safe (shared lock for page lookup)
class NameStore {
private:
    static constexpr size_t kPageSize = 65536;

    struct Page {
        std::array<char, kPageSize> data{};
        uint32_t used = 0;
    };

    std::vector<std::unique_ptr<Page>> pages_;
    mutable std::shared_mutex mutex_;

    Page *pageFor(uint32_t pageId) {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }

    const Page *pageFor(uint32_t pageId) const {
        std::shared_lock lock(mutex_);
        return pages_[pageId].get();
    }

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
    // that can be reused without page lookup on the hot path.
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
            if (page->used >= kPageSize) continue;
            const uint32_t freeBytes = static_cast<uint32_t>(kPageSize - page->used);
            if (static_cast<uint64_t>(freeBytes) * 100 <= static_cast<uint64_t>(kPageSize) * 5) continue;
            cursors.push_back({i, page});
            if (cursors.size() >= maxCount) break;
        }
        return cursors;
    }

    // NOT thread-safe per page. Caller must have exclusive access to cursor.page.
    // If the name doesn't fit, a new page is allocated and the cursor is updated.
    NameRef add(AppendCursor &cursor, std::string_view name) {
        assert(name.size() <= kPageSize);
        Page *page = cursor.page;
        assert(page != nullptr);

        if (page->used + name.size() > kPageSize) {
            cursor = allocateAppendCursor();
            page = cursor.page;
        }

        auto ref = NameRef{
            cursor.pageId,
            static_cast<uint16_t>(page->used),
            static_cast<uint16_t>(name.size()),
        };
        std::memcpy(&page->data[page->used], name.data(), name.size());
        page->used += static_cast<uint32_t>(name.size());
        return ref;
    }

    // Thread-safe (read-only, pages_ never reallocates). Returns the stored name.
    std::string_view get(NameRef ref) const {
        const auto *page = pageFor(ref.pageId);
        return {&page->data[ref.offset], ref.length};
    }

    uint32_t pageCount() const {
        std::shared_lock lock(mutex_);
        return static_cast<uint32_t>(pages_.size());
    }
};

} // namespace ldirstat
