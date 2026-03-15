#pragma once

#include <QTreeView>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"

class QStandardItemModel;
class QStandardItem;

namespace ldirstat {

class DirTreeView : public QTreeView {
    Q_OBJECT

public:
    explicit DirTreeView(QWidget* parent = nullptr);

    void setRoot(const DirEntryStore& store, const NameStore& names, EntryRef root);
    void selectEntry(EntryRef ref);

signals:
    void directorySelected(ldirstat::EntryRef ref);

private slots:
    void onItemExpanded(const QModelIndex& index);
    void onSelectionChanged();

private:
    void populateChildren(QStandardItem* parentItem,
                          const DirEntryStore& store, const NameStore& names,
                          EntryRef parentRef);

    QStandardItemModel* model_ = nullptr;
    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
};

} // namespace ldirstat
