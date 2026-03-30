#pragma once

#include <QString>

#include "direntry.h"
#include "direntrystore.h"
#include "filecategorizer.h"
#include "namestore.h"

namespace ldirstat {

inline QString entryFullPath(const DirEntryStore &store, const NameStore &names, EntryRef ref) {
    std::vector<std::string_view> parts;
    EntryRef cur = ref;
    while (cur.valid()) {
        parts.push_back(names.get(store[cur].name));
        cur = store[cur].parent;
    }
    QString path;
    for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
        if (!path.isEmpty() && !path.endsWith('/')) path += '/';
        path += QString::fromUtf8(parts[i].data(), static_cast<int>(parts[i].size()));
    }
    return path;
}

inline QString formatSizePrecise(uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 1) + " GB";
}

inline QString entryTooltip(const DirEntryStore &store, const NameStore &names, EntryRef ref) {
    const DirEntry &entry = store[ref];
    QString tip = entryFullPath(store, names, ref);
    tip += '\n' + formatSizePrecise(entry.size);
    if (entry.isFile()) {
        tip += '\n' + QStringLiteral("Category: ")
               + QString::fromUtf8(FileCategorizer::displayCategoryName(entry.fileCategory));
        if (entry.hardLinks > 1) {
            tip += '\n' + QString::number(entry.hardLinks) + ' ' + QStringLiteral("hard links");
        }
    }
    if (entry.isDir()) {
        tip += '\n' + QString::number(entry.dirCount) + " dirs, " + QString::number(entry.fileCount) + " files";
    }
    return tip;
}

} // namespace ldirstat
