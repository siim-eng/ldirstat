#pragma once

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
        update();
    }
    void setThemeColors(const ThemeColors& colors) override { themeColors_ = colors; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    static constexpr int kGraphInset = 4;
    static constexpr int kRowHeight = 20;

    QRect graphRect() const;
    EntryRef hitTest(const QPoint& pos) const;

    FlameGraph flameGraph_;
    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
    ThemeColors themeColors_;
    EntryRef selectedEntry_ = kNoEntry;
};

} // namespace ldirstat
