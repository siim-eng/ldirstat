#include "filelistview.h"

#include <QHeaderView>
#include <QStandardItemModel>

#include <algorithm>

namespace ldirstat {

namespace {

QString formatSize(uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}

} // namespace

FileListView::FileListView(QWidget* parent)
    : QTableView(parent) {
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"Name", "Size", "Disk Used"});
    setModel(model_);

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSortingEnabled(false);
    verticalHeader()->hide();

    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
}

void FileListView::showTopFiles(const DirEntryStore& store, const NameStore& names,
                                EntryRef dir, int limit) {
    model_->removeRows(0, model_->rowCount());

    std::vector<EntryRef> files;
    collectFiles(store, dir, files);

    // Partial sort: get top N by size descending.
    int n = std::min(limit, static_cast<int>(files.size()));
    std::partial_sort(files.begin(), files.begin() + n, files.end(),
        [&store](EntryRef a, EntryRef b) {
            return store[a].size > store[b].size;
        });

    for (int i = 0; i < n; ++i) {
        const DirEntry& entry = store[files[i]];
        auto sv = names.get(entry.name);
        auto nameStr = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));

        auto* nameItem = new QStandardItem(nameStr);
        auto* sizeItem = new QStandardItem(formatSize(entry.size));
        auto* diskItem = new QStandardItem(formatSize(entry.blocks * 512));

        model_->appendRow({nameItem, sizeItem, diskItem});
    }
}

void FileListView::collectFiles(const DirEntryStore& store, EntryRef dir,
                                std::vector<EntryRef>& out) {
    std::vector<EntryRef> stack;
    stack.push_back(dir);

    while (!stack.empty()) {
        EntryRef ref = stack.back();
        stack.pop_back();

        const DirEntry& entry = store[ref];
        EntryRef child = entry.firstChild;
        while (child.valid()) {
            const DirEntry& c = store[child];
            if (c.isFile())
                out.push_back(child);
            else if (c.isDir())
                stack.push_back(child);
            child = c.nextSibling;
        }
    }
}

} // namespace ldirstat
