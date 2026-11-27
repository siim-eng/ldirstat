#pragma once

#include "graphwidget.h"
#include "packedtreemap.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QTimer;

namespace ldirstat {

// "Tree Map no headers": fully packed squarified treemap.
class PackedTreeMapWidget : public GraphWidget {
    Q_OBJECT

public:
    explicit PackedTreeMapWidget(QWidget *parent = nullptr);

    void setStores(const DirEntryStore *store, const NameStore *names) override;
    void setDirectory(EntryRef dir) override;
    void setSelectedEntry(EntryRef ref) override {
        selectedEntry_ = ref;
        update();
    }
    void setThemeColors(const ThemeColors &colors) override {
        themeColors_ = colors;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    static constexpr int kGraphInset = 4;
    static constexpr int kResizeDebounceMs = 120;

    QRect graphRect() const;
    void ensureLayout();
    void rebuildLayout();
    EntryRef hitTest(const QPoint &pos);
    void paintTreeMap(QPainter &painter, const QPalette &widgetPalette) const;

    PackedTreeMap treeMap_;
    const DirEntryStore *store_ = nullptr;
    const NameStore *names_ = nullptr;
    ThemeColors themeColors_;
    EntryRef currentDir_;
    EntryRef selectedEntry_ = kNoEntry;
    QSize lastLayoutSize_;
    // Cached nodes may reference entries the current store no longer has.
    bool dataChanged_ = true;
    QTimer *resizeDebounceTimer_ = nullptr;
};

} // namespace ldirstat
