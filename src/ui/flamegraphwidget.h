#pragma once

#include <QPainterPath>

#include <vector>

#include "flamegraph.h"
#include "graphwidget.h"

namespace ldirstat {

class FlameGraphWidget : public GraphWidget {
    Q_OBJECT

public:
    explicit FlameGraphWidget(QWidget* parent = nullptr);

    void setStores(const DirEntryStore* store, const NameStore* names) override;
    void setDirectory(EntryRef dir) override;
    void setSelectedEntry(EntryRef ref) override {
        selectedEntry_ = ref;
        selectionContourDirty_ = true;
        update();
    }
    void setThemeColors(const ThemeColors& colors) override { themeColors_ = colors; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    static constexpr int kGraphInset = 4;
    static constexpr int kRowHeight = 20;

    QRect graphRect() const;
    void ensureLayout();
    void rebuildLayout();
    EntryRef hitTest(const QPoint& pos);
    void ensureSelectionContour();
    void rebuildSelectionContour();
    std::vector<QRect> collectSelectedSubtreeRects(const QRect& graphArea) const;
    bool isInSelectedSubtree(EntryRef ref) const;

    FlameGraph flameGraph_;
    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
    ThemeColors themeColors_;
    EntryRef currentDir_;
    EntryRef selectedEntry_ = kNoEntry;
    QPainterPath selectionContourPath_;
    QRect cachedContourGraphRect_;
    EntryRef cachedContourEntry_ = kNoEntry;
    QSize lastLayoutSize_;
    bool layoutDirty_ = true;
    bool selectionContourDirty_ = true;
};

} // namespace ldirstat
