#pragma once

#include <QColor>
#include <QPalette>

namespace ldirstat {

struct ThemeColors {
    QColor primaryForeground;
    QColor primaryBackground;
    QColor secondaryForeground;
    QColor secondaryBackground;
    QColor selectionBorder;

    static ThemeColors fromPalette(const QPalette& pal) {
        bool dark = pal.color(QPalette::Window).lightness() < 128;
        return {
            dark ? QColor(0x7A, 0x9C, 0xFF) : QColor(0x3E, 0x6F, 0xD1),
            dark ? QColor(0x4A, 0x60, 0x9A) : QColor(0x2A, 0x4A, 0x8C),
            dark ? QColor(0xEB, 0xAD, 0x57) : QColor(0xA0, 0x6A, 0x00),
            dark ? QColor(0x9A, 0x70, 0x38) : QColor(0x6A, 0x46, 0x00),
            dark ? QColor(0xFF, 0x6B, 0x6B) : QColor(0xC4, 0x2B, 0x2B),
        };
    }
};

} // namespace ldirstat
