#include "modifiedtimehistogramdialog.h"

#include "entrytooltip.h"
#include "modifiedtimehistogram.h"
#include "rangeslider.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace ldirstat {

namespace {

enum class HistogramMetricMode : std::uint8_t {
    FileCount,
    TotalFileSize,
};

QString formatHistogramSize(std::uint64_t bytes) {
    auto formatUnit = [](double value, const char *suffix) {
        if (value < 10.0) return QString::number(value, 'f', 1) + ' ' + suffix;
        return QString::number(static_cast<std::uint64_t>(value)) + ' ' + suffix;
    };

    if (bytes < 1024) return QString::number(bytes) + " B";

    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return formatUnit(kb, "KB");

    const double mb = bytes / (1024.0 * 1024.0);
    if (mb < 1024.0) return formatUnit(mb, "MB");

    const double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return formatUnit(gb, "GB");
}

QString formatHistogramMinutes(std::uint32_t minutes) {
    const qint64 seconds = static_cast<qint64>(minutes) * 60;
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone(QTimeZone::LocalTime)).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString formatHistogramDate(std::uint32_t minutes) {
    const qint64 seconds = static_cast<qint64>(minutes) * 60;
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone(QTimeZone::LocalTime)).toString(QStringLiteral("yyyy-MM-dd"));
}

std::uint32_t currentEpochMinutes() {
    const qint64 seconds = QDateTime::currentSecsSinceEpoch();
    if (seconds <= 0) return 0;
    return static_cast<std::uint32_t>(seconds / 60);
}

QDateTime dateTimeForMinutes(std::uint32_t minutes) {
    const qint64 seconds = static_cast<qint64>(minutes) * 60;
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone(QTimeZone::LocalTime));
}

std::uint32_t minutesForDateTime(const QDateTime &dateTime) {
    const qint64 seconds = dateTime.toSecsSinceEpoch();
    if (seconds <= 0) return 0;
    return static_cast<std::uint32_t>(seconds / 60);
}

std::uint32_t minutesForDate(const QDate &date) {
    return minutesForDateTime(QDateTime(date, QTime(0, 0), QTimeZone(QTimeZone::LocalTime)));
}

QDate dateForMinutes(std::uint32_t minutes) {
    return dateTimeForMinutes(minutes).date();
}

std::uint32_t defaultLowerMinutes(const ModifiedTimeHistogramBounds &bounds, std::uint32_t maximumMinutes) {
    if (!bounds.hasKnownFiles) return maximumMinutes;

    const QDate maximumDate = dateForMinutes(maximumMinutes);
    const QDate fiveYearsBack = maximumDate.addYears(-5);
    const std::uint32_t fiveYearsBackMinutes = minutesForDate(fiveYearsBack);
    return std::max(bounds.earliestMinutes, std::min(fiveYearsBackMinutes, maximumMinutes));
}

QString formatPrimaryMetric(const ModifiedTimeHistogramBin &bin, HistogramMetricMode mode) {
    if (mode == HistogramMetricMode::FileCount) return QObject::tr("%1 files").arg(bin.fileCount);
    return formatHistogramSize(bin.totalSize);
}

QString formatBarMetricLabel(const ModifiedTimeHistogramBin &bin, HistogramMetricMode mode) {
    if (mode == HistogramMetricMode::FileCount) return QString::number(bin.fileCount);
    return formatHistogramSize(bin.totalSize);
}

QString formatBreakdownLine(const ModifiedTimeHistogramCategoryValue &value) {
    return QStringLiteral("%1: %2 files, %3")
        .arg(QString::fromUtf8(FileCategorizer::displayCategoryName(value.category)))
        .arg(value.count)
        .arg(formatHistogramSize(value.totalSize));
}

QString formatSelectedRange(std::uint32_t lowerMinutes, std::uint32_t upperMinutes) {
    return QStringLiteral("%1 - %2").arg(formatHistogramDate(lowerMinutes), formatHistogramDate(upperMinutes));
}

std::uint64_t metricValue(const ModifiedTimeHistogramBin &bin, HistogramMetricMode mode) {
    return mode == HistogramMetricMode::FileCount ? bin.fileCount : bin.totalSize;
}

std::uint64_t metricValue(const ModifiedTimeHistogramCategoryValue &value, HistogramMetricMode mode) {
    return mode == HistogramMetricMode::FileCount ? value.count : value.totalSize;
}

HistogramMetricMode metricModeForIndex(int index) {
    return index == 0 ? HistogramMetricMode::FileCount : HistogramMetricMode::TotalFileSize;
}

int sliderIntValue(std::uint32_t minutes) {
    return static_cast<int>(std::min<std::uint32_t>(minutes, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

class ModifiedTimeHistogramWidget : public QWidget {
public:
    explicit ModifiedTimeHistogramWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true);
    }

    void setBins(const std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> &bins) {
        bins_ = bins;
        update();
    }

    void setMetricMode(HistogramMetricMode mode) {
        if (metricMode_ == mode) return;
        metricMode_ = mode;
        update();
    }

    QSize minimumSizeHint() const override { return {720, 620}; }
    QSize sizeHint() const override { return {880, 680}; }

protected:
    void paintEvent(QPaintEvent * /*event*/) override {
        QPainter painter(this);
        const QPalette widgetPalette = palette();
        const QColor background = widgetPalette.color(QPalette::Window);
        const QColor textColor = widgetPalette.color(QPalette::Text);
        painter.fillRect(rect(), background);
        painter.setPen(textColor);

        const QFontMetrics metrics(font());
        const int labelWidth = metrics.horizontalAdvance(QStringLiteral("0000-00-00 00:00"));
        const int leftMargin = 0;
        const int topMargin = 12;
        const int rightMargin = 0;
        const int rowPitch = 24;
        const int rowHeight = 18;
        const int delimiterGap = 10;
        const int delimiterWidth = metrics.horizontalAdvance(QStringLiteral("|"));
        int valueLabelWidth = 0;
        for (const ModifiedTimeHistogramBin &bin : bins_)
            valueLabelWidth = std::max(valueLabelWidth, metrics.horizontalAdvance(formatBarMetricLabel(bin, metricMode_)));
        const int valueLabelGap = valueLabelWidth > 0 ? 10 : 0;
        const int barX = leftMargin + labelWidth + delimiterGap + delimiterWidth + 8;
        const int valueLabelX = std::max(barX, width() - rightMargin - valueLabelWidth);
        const int barWidth = std::max(0, valueLabelX - valueLabelGap - barX);
        const int cellStep = 10;
        const int cellWidth = 8;
        const int cellHeight = 12;
        const int cellCount = std::max(1, barWidth / cellStep);

        std::array<QRect, kModifiedTimeHistogramBinCount> barRects{};
        std::uint64_t maxValue = 0;
        for (const ModifiedTimeHistogramBin &bin : bins_)
            maxValue = std::max(maxValue, metricValue(bin, metricMode_));

        for (std::size_t i = 0; i < bins_.size(); ++i) {
            const ModifiedTimeHistogramBin &bin = bins_[i];
            const int rowY = topMargin + static_cast<int>(i) * rowPitch;
            const int textBaseline = rowY + rowHeight - 4;
            const QString label = formatHistogramMinutes(bin.startMinutes);
            const QString valueLabel = formatBarMetricLabel(bin, metricMode_);

            painter.drawText(leftMargin, textBaseline, label);
            painter.drawText(leftMargin + labelWidth + delimiterGap, textBaseline, QStringLiteral("|"));
            if (!valueLabel.isEmpty()) painter.drawText(valueLabelX, textBaseline, valueLabel);

            const std::uint64_t value = metricValue(bin, metricMode_);
            if (value == 0 || maxValue == 0 || barWidth <= 0) continue;

            int segmentCount = static_cast<int>((static_cast<long double>(value) * cellCount / maxValue) + 0.5L);
            segmentCount = std::clamp(segmentCount, 1, cellCount);

            const int cellY = rowY + (rowHeight - cellHeight) / 2;
            for (int segment = 0; segment < segmentCount; ++segment) {
                const QRect cellRect(barX + segment * cellStep, cellY, cellWidth, cellHeight);
                painter.fillRect(cellRect, textColor);
            }

            const int filledWidth = (segmentCount - 1) * cellStep + cellWidth;
            barRects[i] = QRect(barX, cellY, filledWidth, cellHeight);
        }

        barRects_ = barRects;
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const int binIndex = hitTest(event->pos());
        if (binIndex < 0) {
            hoveredBinIndex_ = -1;
            QToolTip::hideText();
            return;
        }

        if (hoveredBinIndex_ == binIndex && QToolTip::isVisible()) return;
        hoveredBinIndex_ = binIndex;
        QToolTip::showText(event->globalPosition().toPoint(), tooltipForBin(static_cast<std::size_t>(binIndex)), this);
    }

    void leaveEvent(QEvent *event) override {
        hoveredBinIndex_ = -1;
        QToolTip::hideText();
        QWidget::leaveEvent(event);
    }

private:
    int hitTest(const QPoint &pos) const {
        for (std::size_t i = 0; i < barRects_.size(); ++i) {
            if (barRects_[i].isValid() && barRects_[i].contains(pos)) return static_cast<int>(i);
        }
        return -1;
    }

    QString tooltipForBin(std::size_t index) const {
        const ModifiedTimeHistogramBin &bin = bins_[index];
        std::uint32_t endMinutes = bin.endMinutes;
        if (index + 1 < bins_.size() && endMinutes > bin.startMinutes) --endMinutes;

        QStringList lines;
        lines << QStringLiteral("%1 - %2").arg(formatHistogramMinutes(bin.startMinutes), formatHistogramMinutes(endMinutes));
        lines << formatPrimaryMetric(bin, metricMode_);

        std::vector<ModifiedTimeHistogramCategoryValue> categories;
        categories.reserve(bin.categories.size());
        for (const ModifiedTimeHistogramCategoryValue &category : bin.categories) {
            if (category.count == 0 && category.totalSize == 0) continue;
            categories.push_back(category);
        }

        std::sort(categories.begin(), categories.end(), [this](const ModifiedTimeHistogramCategoryValue &lhs,
                                                               const ModifiedTimeHistogramCategoryValue &rhs) {
            const std::uint64_t lhsValue = metricValue(lhs, metricMode_);
            const std::uint64_t rhsValue = metricValue(rhs, metricMode_);
            if (lhsValue != rhsValue) return lhsValue > rhsValue;

            return QString::fromUtf8(FileCategorizer::displayCategoryName(lhs.category))
                   < QString::fromUtf8(FileCategorizer::displayCategoryName(rhs.category));
        });

        for (const ModifiedTimeHistogramCategoryValue &category : categories)
            lines << formatBreakdownLine(category);

        return lines.join('\n');
    }

    std::array<ModifiedTimeHistogramBin, kModifiedTimeHistogramBinCount> bins_{};
    std::array<QRect, kModifiedTimeHistogramBinCount> barRects_{};
    HistogramMetricMode metricMode_ = HistogramMetricMode::FileCount;
    int hoveredBinIndex_ = -1;
};

struct HistogramDialogState {
    ModifiedTimeHistogramBuilder builder;
    ModifiedTimeHistogramBounds bounds;
    std::uint32_t availableMinimumMinutes = 0;
    std::uint32_t availableMaximumMinutes = 0;
    std::uint32_t pendingLowerMinutes = 0;
    std::uint32_t pendingUpperMinutes = 0;
    std::uint32_t appliedLowerMinutes = 0;
    std::uint32_t appliedUpperMinutes = 0;

    explicit HistogramDialogState(const DirEntryStore &store)
        : builder(store) {}
};

struct HistogramDialogWidgets {
    QLabel *summaryLabel = nullptr;
    RangeSlider *rangeSlider = nullptr;
    QLabel *selectedRangeLabel = nullptr;
    QComboBox *metricCombo = nullptr;
    ModifiedTimeHistogramWidget *histogramWidget = nullptr;
    QTimer *debounceTimer = nullptr;
};

std::shared_ptr<HistogramDialogState> createHistogramDialogState(const DirEntryStore &store, EntryRef dirRef) {
    auto state = std::make_shared<HistogramDialogState>(store);
    state->bounds = state->builder.bounds(dirRef);
    state->availableMaximumMinutes = currentEpochMinutes();
    if (state->availableMaximumMinutes == 0) {
        state->availableMaximumMinutes = state->bounds.hasKnownFiles ? state->bounds.latestMinutes : 0;
    }

    state->availableMinimumMinutes = state->bounds.hasKnownFiles
                                         ? std::min(state->bounds.earliestMinutes, state->availableMaximumMinutes)
                                         : state->availableMaximumMinutes;
    state->pendingLowerMinutes = defaultLowerMinutes(state->bounds, state->availableMaximumMinutes);
    state->pendingUpperMinutes = state->availableMaximumMinutes;
    state->appliedLowerMinutes = state->pendingLowerMinutes;
    state->appliedUpperMinutes = state->pendingUpperMinutes;
    return state;
}

void configureDialogShell(ModifiedTimeHistogramDialog *dialog, const QString &directoryPath) {
    dialog->setModal(true);
    dialog->setWindowTitle(QObject::tr("Modified Time Histogram - %1").arg(directoryPath));
    dialog->resize(980, 760);
}

QLabel *createDialogPathLabel(const QString &directoryPath, QWidget *parent) {
    auto *pathLabel = new QLabel(directoryPath, parent);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return pathLabel;
}

QLabel *createDialogSummaryLabel(QWidget *parent) {
    auto *summaryLabel = new QLabel(QObject::tr("Histogram for the current directory and all subdirectories."), parent);
    summaryLabel->setWordWrap(true);
    return summaryLabel;
}

QLabel *createDialogTimestampRuleLabel(QWidget *parent) {
    auto *ruleLabel = new QLabel(
        QObject::tr("The histogram uses modified time, except when a file's creation time is newer than its modified time. "
                    "In that case, creation time is used instead."),
        parent);
    ruleLabel->setWordWrap(true);
    return ruleLabel;
}

HistogramDialogWidgets buildHistogramDialogWidgets(ModifiedTimeHistogramDialog *dialog,
                                                   const QString &directoryPath,
                                                   const HistogramDialogState &state) {
    HistogramDialogWidgets widgets;

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(createDialogPathLabel(directoryPath, dialog));
    widgets.summaryLabel = createDialogSummaryLabel(dialog);
    layout->addWidget(widgets.summaryLabel);
    layout->addWidget(createDialogTimestampRuleLabel(dialog));

    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->addWidget(new QLabel(QObject::tr("Selected Range"), dialog));

    widgets.rangeSlider = new RangeSlider(dialog);
    widgets.rangeSlider->setRange(sliderIntValue(state.availableMinimumMinutes), sliderIntValue(state.availableMaximumMinutes));
    widgets.rangeSlider->setLowerValue(sliderIntValue(state.pendingLowerMinutes));
    widgets.rangeSlider->setUpperValue(sliderIntValue(state.pendingUpperMinutes));
    widgets.rangeSlider->setEnabled(state.bounds.hasKnownFiles);
    controlsLayout->addWidget(widgets.rangeSlider, 1);

    widgets.selectedRangeLabel =
        new QLabel(state.bounds.hasKnownFiles ? formatSelectedRange(state.pendingLowerMinutes, state.pendingUpperMinutes)
                                              : QObject::tr("No known timestamps"),
                   dialog);
    widgets.selectedRangeLabel->setMinimumWidth(dialog->fontMetrics().horizontalAdvance(QStringLiteral("0000-00-00 - 0000-00-00")));
    widgets.selectedRangeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    controlsLayout->addWidget(widgets.selectedRangeLabel);

    controlsLayout->addSpacing(12);
    controlsLayout->addWidget(new QLabel(QObject::tr("Metric"), dialog));

    widgets.metricCombo = new QComboBox(dialog);
    widgets.metricCombo->addItem(QObject::tr("File Count"));
    widgets.metricCombo->addItem(QObject::tr("Total File Size"));
    widgets.metricCombo->setCurrentIndex(1);
    controlsLayout->addWidget(widgets.metricCombo);

    layout->addLayout(controlsLayout);

    widgets.histogramWidget = new ModifiedTimeHistogramWidget(dialog);
    widgets.histogramWidget->setMetricMode(HistogramMetricMode::TotalFileSize);
    layout->addWidget(widgets.histogramWidget, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    widgets.debounceTimer = new QTimer(dialog);
    widgets.debounceTimer->setSingleShot(true);
    widgets.debounceTimer->setInterval(100);

    return widgets;
}

void syncHistogramRangeInputs(const std::shared_ptr<HistogramDialogState> &state, const HistogramDialogWidgets &widgets) {
    state->pendingUpperMinutes = std::clamp(state->pendingUpperMinutes, state->availableMinimumMinutes, state->availableMaximumMinutes);
    state->pendingLowerMinutes = std::clamp(state->pendingLowerMinutes, state->availableMinimumMinutes, state->pendingUpperMinutes);

    {
        const QSignalBlocker sliderBlocker(*widgets.rangeSlider);
        widgets.rangeSlider->setRange(sliderIntValue(state->availableMinimumMinutes), sliderIntValue(state->availableMaximumMinutes));
        widgets.rangeSlider->setLowerValue(sliderIntValue(state->pendingLowerMinutes));
        widgets.rangeSlider->setUpperValue(sliderIntValue(state->pendingUpperMinutes));
    }
}

void applyHistogramDialogState(const std::shared_ptr<HistogramDialogState> &state,
                               EntryRef dirRef,
                               const HistogramDialogWidgets &widgets) {
    state->availableMaximumMinutes = std::max(state->availableMaximumMinutes, currentEpochMinutes());
    syncHistogramRangeInputs(state, widgets);

    state->appliedLowerMinutes = state->pendingLowerMinutes;
    state->appliedUpperMinutes = state->pendingUpperMinutes;
    widgets.selectedRangeLabel->setText(state->bounds.hasKnownFiles
                                            ? formatSelectedRange(state->appliedLowerMinutes, state->appliedUpperMinutes)
                                            : QObject::tr("No known timestamps"));

    widgets.histogramWidget->setBins(state->builder.build(dirRef, state->appliedLowerMinutes, state->appliedUpperMinutes));
    widgets.histogramWidget->setMetricMode(metricModeForIndex(widgets.metricCombo->currentIndex()));

    if (!state->bounds.hasKnownFiles) {
        widgets.summaryLabel->setText(QObject::tr("No files with known modified timestamps were found in this subtree."));
        return;
    }

    widgets.summaryLabel->setText(QObject::tr("Showing 24 bins from %1 to %2 for the current directory and all subdirectories.")
                                      .arg(formatHistogramDate(state->appliedLowerMinutes), formatHistogramDate(state->appliedUpperMinutes)));
}

void connectHistogramDialogSignals(ModifiedTimeHistogramDialog *dialog,
                                   const std::shared_ptr<HistogramDialogState> &state,
                                   EntryRef dirRef,
                                   const HistogramDialogWidgets &widgets) {
    QObject::connect(widgets.rangeSlider,
                     &RangeSlider::rangeChanged,
                     dialog,
                     [state, selectedRangeLabel = widgets.selectedRangeLabel, timer = widgets.debounceTimer](int lower, int upper) {
                         state->pendingLowerMinutes = static_cast<std::uint32_t>(std::max(lower, 0));
                         state->pendingUpperMinutes = static_cast<std::uint32_t>(std::max(upper, 0));
                         selectedRangeLabel->setText(formatSelectedRange(state->pendingLowerMinutes, state->pendingUpperMinutes));
                         timer->start();
                     });

    QObject::connect(widgets.debounceTimer, &QTimer::timeout, dialog, [state, dirRef, widgets]() {
        applyHistogramDialogState(state, dirRef, widgets);
    });

    QObject::connect(widgets.metricCombo,
                     qOverload<int>(&QComboBox::currentIndexChanged),
                     dialog,
                     [histogramWidget = widgets.histogramWidget](int index) { histogramWidget->setMetricMode(metricModeForIndex(index)); });

}

} // namespace

ModifiedTimeHistogramDialog::ModifiedTimeHistogramDialog(const DirEntryStore &store,
                                                         const NameStore &names,
                                                         EntryRef dirRef,
                                                         QWidget *parent)
    : QDialog(parent) {
    const auto state = createHistogramDialogState(store, dirRef);
    const QString directoryPath = QDir::toNativeSeparators(entryFullPath(store, names, dirRef));
    configureDialogShell(this, directoryPath);
    const HistogramDialogWidgets widgets = buildHistogramDialogWidgets(this, directoryPath, *state);
    connectHistogramDialogSignals(this, state, dirRef, widgets);
    applyHistogramDialogState(state, dirRef, widgets);
}

} // namespace ldirstat
