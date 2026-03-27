#pragma once

#include <QByteArray>
#include <QWidget>
#include <vector>

#include "direntry.h"
#include "direntrystore.h"
#include "namestore.h"
#include "themecolors.h"

class QLineEdit;
class QMenu;
class QScrollBar;
class QTimer;
class QToolButton;

namespace ldirstat {

class DirListColumn : public QWidget {
    Q_OBJECT

public:
    explicit DirListColumn(const DirEntryStore& store, const NameStore& names,
                           EntryRef dirRef, uint64_t rootSize,
                           const ThemeColors& themeColors, int columnWidth,
                           QWidget* parent = nullptr);

    EntryRef dirRef() const { return dirRef_; }
    int rowCount() const { return static_cast<int>(visibleRows_.size()); }
    int focusedIndex() const;
    EntryRef focusedRef() const;
    std::vector<EntryRef> selectedRefs() const;
    int selectionCount() const { return selectedCount_; }
    bool hasSelection() const { return selectedCount_ > 0; }
    EntryRef refAtRow(int row) const;
    bool rowIsDir(int row) const;
    void applySelectionAtRow(int row, Qt::KeyboardModifiers modifiers,
                             bool preserveSelection = false);
    void setFocusedIndex(int row, bool selectFocused = true);
    void setFocusedRef(EntryRef ref, bool selectFocused = true);
    void setPathRef(EntryRef ref);
    void setKeyboardActive(bool active);
    void setThemeColors(const ThemeColors& colors);
    void clearSelection();
    void clearFocus();
    void selectAllVisible();
    void invertVisibleSelection();
    void clearFilter();
    void ensureRowVisible(int row);
    void rebuild(uint64_t rootSize);

    // Layout constants exposed for column width calculation.
    static constexpr int kRowHeight = 22;
    static constexpr int kPadding = 4;
    static constexpr int kLeftPadding = 8;
    static constexpr int kArrowSize = 5;

signals:
    void activated();
    void focusChanged(ldirstat::EntryRef ref, bool isDir);
    void contextMenuRequested(ldirstat::EntryRef ref, QPoint globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
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
        bool isMountPoint;
        SizeTier sizeTier;
    };

    static SizeTier sizeTierFor(uint64_t bytes);

    void buildChildList();
    void rebuildVisibleRows();
    void applyFilter();
    int childIndexAtRow(int row) const;
    int childIndexForRef(EntryRef ref) const;
    int hitTestRow(const QPoint& pos) const;
    void layoutChildWidgets();
    void applyMouseSelection(int childIndex, Qt::KeyboardModifiers modifiers,
                             bool preserveSelection);
    void setSingleSelection(int childIndex);
    void setSelectionState(int childIndex, bool selected);
    void selectVisibleRange(int firstChildIndex, int lastChildIndex);
    void emitFocusChanged();
    QRect listRect() const;
    QRect footerRect() const;
    void updateScrollBar();
    void paintRows(QPainter& painter, const QRect& listRect);
    void paintFooter(QPainter& painter, const QRect& footerRect);

    static constexpr int kFooterHeight = 28;
    static constexpr int kFooterGap = 2;
    static constexpr int kFilterGap = 6;
    static constexpr int kFilterBottomPadding = 6;
    static constexpr int kFilterButtonGap = 4;

    const DirEntryStore& store_;
    const NameStore& names_;
    EntryRef dirRef_;
    uint64_t rootSize_;
    ThemeColors themeColors_;
    std::vector<ChildEntry> children_;
    std::vector<int> visibleRows_;
    std::vector<int> visibleRowByChild_;
    std::vector<uint8_t> selectionFlags_;
    int focusedChildIndex_ = -1;
    int anchorChildIndex_ = -1;
    int pathChildIndex_ = -1;
    int selectedCount_ = 0;
    bool keyboardActive_ = false;
    QScrollBar* scrollBar_;
    QLineEdit* filterEdit_;
    QToolButton* filterMenuButton_;
    QMenu* filterMenu_;
    QTimer* filterTimer_;
    QByteArray appliedFilterUtf8_;
    QColor pathHighlightColor_;

    // Cached field widths.
    int sizeFieldWidth_ = 0;
    int pctFieldWidth_ = 0;

    // Footer stats.
    uint32_t footerDirs_ = 0;
    uint32_t footerFiles_ = 0;
    uint64_t footerBytes_ = 0;
};

} // namespace ldirstat
