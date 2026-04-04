#pragma once

#include <cstdint>

#include <QWidget>

#include "themecolors.h"

namespace ldirstat {

class RangeSlider : public QWidget {
    Q_OBJECT

public:
    explicit RangeSlider(QWidget *parent = nullptr);

    int minimum() const { return minimum_; }
    int maximum() const { return maximum_; }

    int lowerValue() const { return lowerValue_; }
    int upperValue() const { return upperValue_; }

    void setMinimum(int value);
    void setMaximum(int value);
    void setRange(int minimum, int maximum);

    void setLowerValue(int value);
    void setUpperValue(int value);
    void setThemeColors(const ThemeColors &colors);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void rangeChanged(int lower, int upper);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class ActiveHandle : uint8_t {
        None,
        Lower,
        Upper,
    };

    QRect trackRect() const;
    QRect selectedRect(const QRect &track) const;
    QRect lowerHandleRect(const QRect &track) const;
    QRect upperHandleRect(const QRect &track) const;
    int xForValue(int value, const QRect &track) const;
    int valueForX(int x, const QRect &track) const;
    void setLowerValueInternal(int value, bool emitSignals);
    void setUpperValueInternal(int value, bool emitSignals);
    void emitRangeSignals(bool lowerChanged, bool upperChanged);
    ActiveHandle pickHandle(const QPoint &pos, const QRect &track) const;

    int minimum_ = 0;
    int maximum_ = 100;
    int lowerValue_ = 0;
    int upperValue_ = 100;
    ActiveHandle activeHandle_ = ActiveHandle::None;
    bool dragging_ = false;
    ThemeColors themeColors_;
};

} // namespace ldirstat
