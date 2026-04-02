#include "modifiedtimehistogram.h"

#include <algorithm>
#include <vector>

namespace ldirstat {

namespace {

std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> makeEmptyHistogram(std::uint32_t startMinutes,
                                                                                         std::uint32_t nowMinutes) {
    std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> bins{};
    const std::uint64_t range = nowMinutes > startMinutes ? static_cast<std::uint64_t>(nowMinutes - startMinutes) : 0;

    for (std::size_t i = 0; i < bins.size(); ++i) {
        ModifiedTimeHistogramBin &bin = bins[i];
        bin.startMinutes = startMinutes + static_cast<std::uint32_t>((range * i) / bins.size());
        bin.endMinutes = i + 1 < bins.size()
                             ? startMinutes + static_cast<std::uint32_t>((range * (i + 1)) / bins.size())
                             : nowMinutes;

        for (std::size_t categoryIndex = 0; categoryIndex < bin.categories.size(); ++categoryIndex) {
            bin.categories[categoryIndex].category = static_cast<FileCategory>(categoryIndex);
        }
    }

    return bins;
}

std::size_t binIndexFor(std::uint32_t modifiedMinutes, std::uint32_t startMinutes, std::uint32_t nowMinutes) {
    if (nowMinutes <= startMinutes) return kModifiedTimeHistogramBinCount - 1;
    if (modifiedMinutes <= startMinutes) return 0;
    if (modifiedMinutes >= nowMinutes) return kModifiedTimeHistogramBinCount - 1;

    const std::uint64_t range = static_cast<std::uint64_t>(nowMinutes - startMinutes);
    if (range == 0) return kModifiedTimeHistogramBinCount - 1;

    const std::uint64_t offset = static_cast<std::uint64_t>(modifiedMinutes - startMinutes);
    const std::uint64_t index = (offset * kModifiedTimeHistogramBinCount) / range;
    return std::min<std::size_t>(kModifiedTimeHistogramBinCount - 1, static_cast<std::size_t>(index));
}

} // namespace

ModifiedTimeHistogramBounds ModifiedTimeHistogramBuilder::bounds(EntryRef root) const {
    ModifiedTimeHistogramBounds result;

    forEachFile(root, [&result](const DirEntry &entry) {
        const std::uint32_t modifiedMinutes = entry.modifiedMinutes();
        if (modifiedMinutes == 0) return;

        if (!result.hasKnownFiles) {
            result.earliestMinutes = modifiedMinutes;
            result.latestMinutes = modifiedMinutes;
            result.hasKnownFiles = true;
            return;
        }

        result.earliestMinutes = std::min(result.earliestMinutes, modifiedMinutes);
        result.latestMinutes = std::max(result.latestMinutes, modifiedMinutes);
    });

    return result;
}

std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> ModifiedTimeHistogramBuilder::build(
    EntryRef root, std::uint32_t startMinutes, std::uint32_t nowMinutes) const {
    if (nowMinutes < startMinutes) nowMinutes = startMinutes;

    auto bins = makeEmptyHistogram(startMinutes, nowMinutes);

    forEachFile(root, [&bins, startMinutes, nowMinutes](const DirEntry &entry) {
        const std::uint32_t modifiedMinutes = entry.modifiedMinutes();
        if (modifiedMinutes == 0 || modifiedMinutes < startMinutes) return;

        ModifiedTimeHistogramBin &bin = bins[binIndexFor(modifiedMinutes, startMinutes, nowMinutes)];
        const std::uint64_t fileSize = layoutSizeOf(entry);
        const std::size_t categoryIndex = FileCategorizer::categoryIndex(FileCategorizer::categoryForType(entry.fileType));

        ++bin.fileCount;
        bin.totalSize += fileSize;
        ++bin.categories[categoryIndex].count;
        bin.categories[categoryIndex].totalSize += fileSize;
    });

    return bins;
}

template<typename Visitor> void ModifiedTimeHistogramBuilder::forEachFile(EntryRef root, Visitor &&visit) const {
    if (!root.valid()) return;

    const DirEntry &rootEntry = entryStore_[root];
    if (rootEntry.isFile()) {
        visit(rootEntry);
        return;
    }

    std::vector<EntryRef> stack;
    stack.reserve(256);
    stack.push_back(root);

    EntryRef child = rootEntry.firstChild;
    bool dirPopped = false;
    while (!stack.empty() && child.valid()) {
        const DirEntry &entry = entryStore_[child];
        if (entry.isDir() && entry.childCount > 0 && !dirPopped) {
            stack.push_back(child);
            child = entry.firstChild;
            continue;
        }

        if (entry.isFile()) visit(entry);

        dirPopped = false;
        child = entry.nextSibling;
        if (!child.valid()) {
            child = stack.back();
            stack.pop_back();
            dirPopped = true;
        }
    }
}

} // namespace ldirstat
