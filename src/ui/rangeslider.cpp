#include "rangeslider.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace ldirstat {

namespace {

constexpr int kOuterMargin = 8;
constexpr int kTrackHeight = 8;
constexpr int kHandleWidth = 10;
constexpr int kHandleHeight = 18;
constexpr int kSegmentStep = 10;
constexpr int kSegmentWidth = 8;

} // namespace

RangeSlider::RangeSlider(QWidget *parent)
    : QWidget(parent) {
    setMouseTracking(true);
}

void RangeSlider::setMinimum(int value) {
    setRange(value, maximum_);
}

void RangeSlider::setMaximum(int value) {
    setRange(minimum_, value);
}

void RangeSlider::setRange(int minimum, int maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    if (minimum_ == minimum && maximum_ == maximum) return;

    minimum_ = minimum;
    maximum_ = maximum;

    const int clampedLower = std::clamp(lowerValue_, minimum_, maximum_);
    const int clampedUpper = std::clamp(upperValue_, clampedLower, maximum_);
    const bool lowerChanged = clampedLower != lowerValue_;
    const bool upperChanged = clampedUpper != upperValue_;
    lowerValue_ = clampedLower;
    upperValue_ = clampedUpper;

    emitRangeSignals(lowerChanged, upperChanged);
    update();
}

void RangeSlider::setLowerValue(int value) {
    setLowerValueInternal(value, true);
}

void RangeSlider::setUpperValue(int value) {
    setUpperValueInternal(value, true);
}

QSize RangeSlider::minimumSizeHint() const {
    return {120, 28};
}

QSize RangeSlider::sizeHint() const {
    return {320, 32};
}

void RangeSlider::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    const QPalette widgetPalette = palette();
    const QColor background = widgetPalette.color(QPalette::Window);
    const QColor textColor = widgetPalette.color(QPalette::Text);
    const QColor inactiveColor = widgetPalette.color(QPalette::Mid);
    painter.fillRect(rect(), background);

    const QRect track = trackRect();
    const QRect selected = selectedRect(track);

    const int segmentCount = std::max(1, track.width() / kSegmentStep);
    for (int i = 0; i < segmentCount; ++i) {
        const QRect segment(track.left() + i * kSegmentStep, track.top(), kSegmentWidth, track.height());
        painter.fillRect(segment, segment.intersects(selected) ? textColor : inactiveColor);
    }

    const QRect lowerRect = lowerHandleRect(track);
    const QRect upperRect = upperHandleRect(track);

    painter.setPen(textColor);
    painter.drawRect(lowerRect);
    painter.drawRect(upperRect);
    painter.fillRect(lowerRect.adjusted(2, 2, -2, -2), textColor);
    painter.fillRect(upperRect.adjusted(2, 2, -2, -2), textColor);
}

void RangeSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QRect track = trackRect();
    activeHandle_ = pickHandle(event->pos(), track);
    dragging_ = activeHandle_ != ActiveHandle::None;
    if (!dragging_) return;

    const int value = valueForX(event->position().toPoint().x(), track);
    if (activeHandle_ == ActiveHandle::Lower)
        setLowerValue(value);
    else
        setUpperValue(value);
}

void RangeSlider::mouseMoveEvent(QMouseEvent *event) {
    const QRect track = trackRect();

    if (!dragging_) {
        const ActiveHandle hoverHandle = pickHandle(event->pos(), track);
        setCursor(hoverHandle == ActiveHandle::None ? Qt::ArrowCursor : Qt::SizeHorCursor);
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int value = valueForX(event->position().toPoint().x(), track);
    if (activeHandle_ == ActiveHandle::Lower)
        setLowerValue(value);
    else if (activeHandle_ == ActiveHandle::Upper)
        setUpperValue(value);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        activeHandle_ = ActiveHandle::None;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void RangeSlider::leaveEvent(QEvent *event) {
    if (!dragging_) setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

QRect RangeSlider::trackRect() const {
    const int left = kOuterMargin + kHandleWidth / 2;
    const int right = width() - kOuterMargin - kHandleWidth / 2;
    const int trackWidth = std::max(1, right - left);
    const int top = (height() - kTrackHeight) / 2;
    return {left, top, trackWidth, kTrackHeight};
}

QRect RangeSlider::selectedRect(const QRect &track) const {
    const int x1 = xForValue(lowerValue_, track);
    const int x2 = xForValue(upperValue_, track);
    return QRect(std::min(x1, x2), track.top(), std::max(1, std::abs(x2 - x1)), track.height());
}

QRect RangeSlider::lowerHandleRect(const QRect &track) const {
    const int centerX = xForValue(lowerValue_, track);
    return QRect(centerX - kHandleWidth / 2, (height() - kHandleHeight) / 2, kHandleWidth, kHandleHeight);
}

QRect RangeSlider::upperHandleRect(const QRect &track) const {
    const int centerX = xForValue(upperValue_, track);
    return QRect(centerX - kHandleWidth / 2, (height() - kHandleHeight) / 2, kHandleWidth, kHandleHeight);
}

int RangeSlider::xForValue(int value, const QRect &track) const {
    if (maximum_ <= minimum_) return track.left();
    const double ratio = static_cast<double>(value - minimum_) / static_cast<double>(maximum_ - minimum_);
    return track.left() + static_cast<int>(std::lround(ratio * track.width()));
}

int RangeSlider::valueForX(int x, const QRect &track) const {
    if (maximum_ <= minimum_ || track.width() <= 0) return minimum_;

    const int clampedX = std::clamp(x, track.left(), track.right());
    const double ratio = static_cast<double>(clampedX - track.left()) / static_cast<double>(track.width());
    return minimum_ + static_cast<int>(std::lround(ratio * (maximum_ - minimum_)));
}

void RangeSlider::setLowerValueInternal(int value, bool emitSignals) {
    const int clamped = std::clamp(value, minimum_, upperValue_);
    if (clamped == lowerValue_) return;
    lowerValue_ = clamped;
    if (emitSignals) emitRangeSignals(true, false);
    update();
}

void RangeSlider::setUpperValueInternal(int value, bool emitSignals) {
    const int clamped = std::clamp(value, lowerValue_, maximum_);
    if (clamped == upperValue_) return;
    upperValue_ = clamped;
    if (emitSignals) emitRangeSignals(false, true);
    update();
}

void RangeSlider::emitRangeSignals(bool lowerChanged, bool upperChanged) {
    if (lowerChanged) emit lowerValueChanged(lowerValue_);
    if (upperChanged) emit upperValueChanged(upperValue_);
    if (lowerChanged || upperChanged) emit rangeChanged(lowerValue_, upperValue_);
}

RangeSlider::ActiveHandle RangeSlider::pickHandle(const QPoint &pos, const QRect &track) const {
    const QRect lowerRect = lowerHandleRect(track);
    const QRect upperRect = upperHandleRect(track);

    if (lowerRect.contains(pos) && upperRect.contains(pos)) return ActiveHandle::Lower;
    if (lowerRect.contains(pos)) return ActiveHandle::Lower;
    if (upperRect.contains(pos)) return ActiveHandle::Upper;

    const int lowerDistance = std::abs(pos.x() - lowerRect.center().x());
    const int upperDistance = std::abs(pos.x() - upperRect.center().x());
    return lowerDistance <= upperDistance ? ActiveHandle::Lower : ActiveHandle::Upper;
}

} // namespace ldirstat
