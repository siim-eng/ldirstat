#pragma once

#include <QDialog>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"

namespace ldirstat {

class ModifiedTimeHistogramDialog : public QDialog {
public:
    explicit ModifiedTimeHistogramDialog(const DirEntryStore &store, const NameStore &names, EntryRef dirRef, QWidget *parent = nullptr);
};

} // namespace ldirstat
