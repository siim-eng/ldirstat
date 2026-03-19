#pragma once

#include <QWidget>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

class QScrollBar;
class QTimer;

namespace ldirstat {

class DirListColumn : public QWidget {
    Q_OBJECT

public:
    explicit DirListColumn(const DirEntryStore& store, const NameStore& names,
                           EntryRef dirRef, uint64_t rootSize,
                           const ThemeColors& themeColors, int columnWidth,
                           QWidget* parent = nullptr);

    EntryRef dirRef() const { return dirRef_; }
    EntryRef selectedRef() const;
    void setSelectedRef(EntryRef ref);
    void setThemeColors(const ThemeColors& colors);
    void clearSelection();

    // Layout constants exposed for column width calculation.
    static constexpr int kRowHeight = 22;
    static constexpr int kPadding = 4;
    static constexpr int kLeftPadding = 8;
    static constexpr int kArrowSize = 5;

signals:
    void entryClicked(ldirstat::EntryRef ref, bool isDir);
    void contextMenuRequested(ldirstat::EntryRef ref, QPoint globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class SizeTier : uint8_t { Bytes, KB, MB, GB };

    struct ChildEntry {
        EntryRef ref;
        QString sizeStr;
        QString pctStr;
        QString name;
        bool isDir;
        SizeTier sizeTier;
    };

    static SizeTier sizeTierFor(uint64_t bytes);

    void buildChildList();
    int hitTestRow(const QPoint& pos) const;
    void updateScrollBar();

    static constexpr int kFooterHeight = 28;
    static constexpr int kFooterGap = 2;

    const DirEntryStore& store_;
    const NameStore& names_;
    EntryRef dirRef_;
    uint64_t rootSize_;
    ThemeColors themeColors_;
    std::vector<ChildEntry> children_;
    int selectedIndex_ = -1;
    QScrollBar* scrollBar_;
    QTimer* tooltipTimer_;
    int hoverRow_ = -1;
    QPoint hoverGlobalPos_;

    // Cached field widths.
    int sizeFieldWidth_ = 0;
    int pctFieldWidth_ = 0;

    // Footer stats.
    uint32_t footerDirs_ = 0;
    uint32_t footerFiles_ = 0;
    uint64_t footerBytes_ = 0;
};

} // namespace ldirstat
