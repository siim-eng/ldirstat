#pragma once

#include <QWidget>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

namespace ldirstat {

class GraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit GraphWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    ~GraphWidget() override = default;

    virtual void setStores(const DirEntryStore* store, const NameStore* names) = 0;
    virtual void setDirectory(EntryRef dir) = 0;
    virtual void setThemeColors(const ThemeColors& colors) = 0;

signals:
    void entrySelected(ldirstat::EntryRef ref);
};

} // namespace ldirstat
