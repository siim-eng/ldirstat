#pragma once

#include <QDialog>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

namespace ldirstat {

class ModifiedTimeHistogramDialog : public QDialog {
public:
    explicit ModifiedTimeHistogramDialog(const DirEntryStore &store,
                                         const NameStore &names,
                                         EntryRef dirRef,
                                         const ThemeColors &themeColors,
                                         QWidget *parent = nullptr);
};

} // namespace ldirstat
