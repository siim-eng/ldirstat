#pragma once

#include <QTableView>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"

class QStandardItemModel;

namespace ldirstat {

class FileListView : public QTableView {
    Q_OBJECT

public:
    explicit FileListView(QWidget* parent = nullptr);

    void showTopFiles(const DirEntryStore& store, const NameStore& names,
                      EntryRef dir, int limit = 100);

private:
    void collectFiles(const DirEntryStore& store, EntryRef dir,
                      std::vector<EntryRef>& out);

    QStandardItemModel* model_ = nullptr;
};

} // namespace ldirstat
