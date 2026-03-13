#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace ldirstat {

struct NameRef {
    uint16_t page_id = 0;
    uint16_t offset  = 0;
    uint16_t length  = 0;
};

static_assert(sizeof(NameRef) == 6);

// Page-based string storage. 64KB pages, no resizes — only new pages are added.
// Thread safety:
//   - allocate_page(): thread-safe (mutex-protected)
//   - add():           NOT thread-safe per page — caller must own the page exclusively
//   - get():           thread-safe (read-only, pages_ vector never reallocates)
class NameStore {
    static constexpr size_t kPageSize = 65536;
    static constexpr size_t kMaxPages = 65535;

    struct Page {
        std::array<char, kPageSize> data{};
        uint32_t used = 0;
    };

    std::vector<std::unique_ptr<Page>> pages_;
    std::mutex mutex_;

public:
    NameStore() { pages_.reserve(kMaxPages); }

    // Thread-safe. Allocates a new empty page, returns its ID.
    uint16_t allocate_page() {
        auto page = std::make_unique<Page>();
        std::lock_guard<std::mutex> lock(mutex_);
        assert(pages_.size() < kMaxPages);
        auto id = static_cast<uint16_t>(pages_.size());
        pages_.push_back(std::move(page));
        return id;
    }

    // NOT thread-safe per page. Caller must have exclusive access to current_page.
    // If the name doesn't fit, a new page is allocated and current_page is updated.
    NameRef add(uint16_t& current_page, std::string_view name) {
        assert(name.size() <= kPageSize);
        assert(current_page < pages_.size());

        auto* page = pages_[current_page].get();
        if (page->used + name.size() > kPageSize) {
            current_page = allocate_page();
            page = pages_[current_page].get();
        }

        auto ref = NameRef{
            current_page,
            static_cast<uint16_t>(page->used),
            static_cast<uint16_t>(name.size()),
        };
        std::memcpy(&page->data[page->used], name.data(), name.size());
        page->used += static_cast<uint32_t>(name.size());
        return ref;
    }

    // Thread-safe (read-only, pages_ never reallocates). Returns the stored name.
    std::string_view get(NameRef ref) const {
        return {&pages_[ref.page_id]->data[ref.offset], ref.length};
    }
};

} // namespace ldirstat
