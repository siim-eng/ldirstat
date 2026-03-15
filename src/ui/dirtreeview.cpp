#include "dirtreeview.h"

#include <QHeaderView>
#include <QStandardItemModel>

namespace ldirstat {

namespace {

// Store EntryRef in QStandardItem's data.
constexpr int kEntryRefRole = Qt::UserRole + 1;
constexpr int kPopulatedRole = Qt::UserRole + 2;

QVariant entryRefToVariant(EntryRef ref) {
    return QVariant::fromValue(static_cast<quint32>(
        (static_cast<quint32>(ref.pageId) << 16) | ref.index));
}

EntryRef variantToEntryRef(const QVariant& v) {
    quint32 packed = v.toUInt();
    return {static_cast<uint16_t>(packed >> 16),
            static_cast<uint16_t>(packed & 0xFFFF)};
}

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

DirTreeView::DirTreeView(QWidget* parent)
    : QTreeView(parent) {
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"Name", "Size"});
    setModel(model_);

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(this, &QTreeView::expanded, this, &DirTreeView::onItemExpanded);
    selectionConn_ = connect(selectionModel(), &QItemSelectionModel::selectionChanged,
                             this, &DirTreeView::onSelectionChanged);
}

void DirTreeView::setRoot(const DirEntryStore& store, const NameStore& names,
                          EntryRef root) {
    store_ = &store;
    names_ = &names;

    model_->clear();
    model_->setHorizontalHeaderLabels({"Name", "Size"});

    // model_->clear() resets the selectionModel, so reconnect (old one is gone).
    disconnect(selectionConn_);
    selectionConn_ = connect(selectionModel(), &QItemSelectionModel::selectionChanged,
                             this, &DirTreeView::onSelectionChanged);

    const DirEntry& entry = store[root];
    auto nameStr = QString::fromUtf8(names.get(entry.name).data(),
                                     static_cast<int>(names.get(entry.name).size()));

    auto* nameItem = new QStandardItem(nameStr);
    nameItem->setEditable(false);
    nameItem->setData(entryRefToVariant(root), kEntryRefRole);
    nameItem->setData(false, kPopulatedRole);

    auto* sizeItem = new QStandardItem(formatSize(entry.size));
    sizeItem->setEditable(false);

    // Add placeholder child so the expand arrow shows.
    if (entry.firstChild.valid())
        nameItem->appendRow(new QStandardItem(""));

    model_->appendRow({nameItem, sizeItem});
    expand(model_->index(0, 0));
}

void DirTreeView::selectEntry(EntryRef /*ref*/) {
    // Minimal: full path-based tree walk not implemented yet.
    // Would need to walk parent chain, expand each, then select.
}

void DirTreeView::onItemExpanded(const QModelIndex& index) {
    if (!store_ || !names_)
        return;

    auto* item = model_->itemFromIndex(index);
    if (!item || item->data(kPopulatedRole).toBool())
        return;

    EntryRef ref = variantToEntryRef(item->data(kEntryRefRole));
    item->removeRows(0, item->rowCount()); // remove placeholder
    populateChildren(item, *store_, *names_, ref);
    item->setData(true, kPopulatedRole);
}

void DirTreeView::onSelectionChanged() {
    auto indexes = selectionModel()->selectedIndexes();
    if (indexes.isEmpty())
        return;

    // Column 0 holds the EntryRef data.
    QModelIndex idx = indexes.first();
    if (idx.column() != 0)
        idx = idx.siblingAtColumn(0);

    auto* item = model_->itemFromIndex(idx);
    if (!item)
        return;

    EntryRef ref = variantToEntryRef(item->data(kEntryRefRole));
    if (store_ && (*store_)[ref].isDir())
        emit directorySelected(ref);
}

void DirTreeView::populateChildren(QStandardItem* parentItem,
                                   const DirEntryStore& store,
                                   const NameStore& names,
                                   EntryRef parentRef) {
    const DirEntry& parent = store[parentRef];
    EntryRef childRef = parent.firstChild;

    while (childRef.valid()) {
        const DirEntry& child = store[childRef];
        auto sv = names.get(child.name);
        auto nameStr = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));

        auto* nameItem = new QStandardItem(nameStr);
        nameItem->setEditable(false);
        nameItem->setData(entryRefToVariant(childRef), kEntryRefRole);
        nameItem->setData(false, kPopulatedRole);

        auto* sizeItem = new QStandardItem(formatSize(child.size));
        sizeItem->setEditable(false);

        // Directories get a placeholder for the expand arrow.
        if (child.isDir() && child.firstChild.valid())
            nameItem->appendRow(new QStandardItem(""));

        parentItem->appendRow({nameItem, sizeItem});
        childRef = child.nextSibling;
    }
}

} // namespace ldirstat
