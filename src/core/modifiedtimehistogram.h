#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "filecategorizer.h"

namespace ldirstat {

inline constexpr std::size_t kModifiedTimeHistogramBinCount = 24;

struct ModifiedTimeHistogramCategoryValue {
    FileCategory category = FileCategory::Unknown;
    std::uint64_t count = 0;
    std::uint64_t totalSize = 0;
};

struct ModifiedTimeHistogramBin {
    std::uint32_t startMinutes = 0;
    std::uint32_t endMinutes = 0;
    std::uint64_t fileCount = 0;
    std::uint64_t totalSize = 0;
    std::array<ModifiedTimeHistogramCategoryValue, FileCategorizer::kCategoryCount> categories{};
};

struct ModifiedTimeHistogramBounds {
    std::uint32_t earliestMinutes = 0;
    std::uint32_t latestMinutes = 0;
    bool hasKnownFiles = false;
};

class ModifiedTimeHistogramBuilder {
public:
    explicit ModifiedTimeHistogramBuilder(const DirEntryStore &entryStore)
        : entryStore_(entryStore) {}

    ModifiedTimeHistogramBounds bounds(EntryRef root) const;
    std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> build(EntryRef root,
                                                                               std::uint32_t startMinutes,
                                                                               std::uint32_t endMinutes) const;

private:
    template<typename Visitor> void forEachFile(EntryRef root, Visitor &&visit) const;

    const DirEntryStore &entryStore_;
};

} // namespace ldirstat
