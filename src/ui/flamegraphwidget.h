#pragma once

#include <QWidget>

#include "direntry.h"
#include "direntrystore.h"
#include "flamegraph.h"
#include "namestore.h"
#include "themecolors.h"

namespace ldirstat {

class FlameGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit FlameGraphWidget(QWidget* parent = nullptr);

    void setThemeColors(const ThemeColors& colors) { themeColors_ = colors; update(); }
    void setGraph(const FlameGraph* graph, const DirEntryStore* store,
                  const NameStore* names);

signals:
    void rectClicked(ldirstat::EntryRef ref);
    void rectHovered(ldirstat::EntryRef ref);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    static constexpr int kGraphInset = 4;
    static constexpr int kRowHeight = 20;

    QRect graphRect() const;
    EntryRef hitTest(const QPoint& pos) const;

    const FlameGraph* graph_ = nullptr;
    const DirEntryStore* store_ = nullptr;
    const NameStore* names_ = nullptr;
    ThemeColors themeColors_;
};

} // namespace ldirstat
