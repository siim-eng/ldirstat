#pragma once

#include "graphwidget.h"
#include "treemap.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;

namespace ldirstat {

class TreeMapWidget : public GraphWidget {
    Q_OBJECT

public:
    enum class RenderMode : uint8_t {
        Packed,
        DirectoryHeaders,
    };

    explicit TreeMapWidget(QWidget* parent = nullptr);

    void setStores(const DirEntryStore* store, const NameStore* names) override;
    void setDirectory(EntryRef dir) override;
    void setSelectedEntry(EntryRef ref) override {
        selectedEntry_ = ref;
        update();
    }
    void setThemeColors(const ThemeColors& colors) override {
        themeColors_ = colors;
        update();
    }

    void setRenderMode(RenderMode mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    static constexpr int kGraphInset = 4;

    QRect graphRect() const;
    void ensureLayout();
    void rebuildLayout();
    EntryRef hitTest(const QPoint& pos);

    void paintPacked(QPainter& painter, const QPalette& widgetPalette) const;
    void paintDirectoryHeaders(QPainter& painter, const QPalette& widgetPalette) const;

    RenderMode renderMode_ = RenderMode::DirectoryHeaders;
    TreeMap treeMap_;
    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
    ThemeColors themeColors_;
    EntryRef currentDir_;
    EntryRef selectedEntry_ = kNoEntry;
    QSize lastLayoutSize_;
    bool layoutDirty_ = true;
};

} // namespace ldirstat
