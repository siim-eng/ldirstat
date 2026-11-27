#pragma once

#include "graphwidget.h"
#include "headertreemap.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QTimer;

namespace ldirstat {

// "Tree Map - Directory Headers": every directory is a framed box with a
// name header and its children inset inside it.
class HeaderTreeMapWidget : public GraphWidget {
    Q_OBJECT

public:
    explicit HeaderTreeMapWidget(QWidget *parent = nullptr);

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

    HeaderTreeMap treeMap_;
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
