#pragma once

#include <array>

#include <QColor>
#include <QPalette>

#include "filecategorizer.h"

namespace ldirstat {

struct ThemeColors {
    QColor primaryForeground;
    QColor primaryBackground;
    QColor secondaryForeground;
    QColor secondaryBackground;
    QColor mountForeground;
    QColor selectionBorder;
    std::array<QColor, FileCategorizer::kCategoryCount> fileCategoryBackgrounds;

    [[nodiscard]] const QColor &colorForFileCategory(FileCategory category) const {
        return fileCategoryBackgrounds[FileCategorizer::categoryIndex(category)];
    }

    static ThemeColors fromPalette(const QPalette &pal) {
        const bool dark = pal.color(QPalette::Window).lightness() < 128;

        ThemeColors c;
        c.primaryForeground  = dark ? QColor(0x7A, 0x9C, 0xFF) : QColor(0x3E, 0x6F, 0xD1);
        c.primaryBackground  = dark ? QColor(0x4A, 0x60, 0x9A) : QColor(0x2A, 0x4A, 0x8C);
        c.secondaryForeground = dark ? QColor(0xEB, 0xAD, 0x57) : QColor(0xA0, 0x6A, 0x00);
        c.secondaryBackground = dark ? QColor(0x9A, 0x70, 0x38) : QColor(0x6A, 0x46, 0x00);
        c.mountForeground    = dark ? QColor(0xFF, 0x7B, 0x72) : QColor(0xC0, 0x36, 0x45);
        c.selectionBorder    = dark ? QColor(0xFF, 0x6B, 0x6B) : QColor(0xC4, 0x2B, 0x2B);

        auto &bg = c.fileCategoryBackgrounds;
        const auto setBg = [&](FileCategory category, int red, int green, int blue) {
            bg[FileCategorizer::categoryIndex(category)] = QColor(red, green, blue);
        };
        setBg(FileCategory::Unknown, 0x7A, 0x7A, 0x7A);
        setBg(FileCategory::Archive, 0x8C, 0x62, 0x39);
        setBg(FileCategory::Compressed, 0xC9, 0x8A, 0x00);
        setBg(FileCategory::Database, 0x2C, 0x7A, 0x7B);
        setBg(FileCategory::DiskImage, 0x51, 0x66, 0xB5);
        setBg(FileCategory::Document, 0x2F, 0x6D, 0xB3);
        setBg(FileCategory::Package, 0xA3, 0x5A, 0x3A);
        setBg(FileCategory::Image, 0x2F, 0x9D, 0x73);
        setBg(FileCategory::BackupTemp, 0xB2, 0x8A, 0x3A);
        setBg(FileCategory::Cache, 0x7C, 0x6A, 0xA6);
        setBg(FileCategory::Library, 0x60, 0x7D, 0x8B);
        setBg(FileCategory::Log, 0xC7, 0x6B, 0x29);
        setBg(FileCategory::Music, 0xC4, 0x4E, 0x7A);
        setBg(FileCategory::ObjectGenerated, 0x6E, 0x7F, 0x91);
        setBg(FileCategory::Source, 0x2F, 0x8F, 0xA3);
        setBg(FileCategory::Video, 0xB3, 0x4A, 0x4A);
        setBg(FileCategory::Executable, 0x4C, 0x9A, 0x2A);

        return c;
    }
};

} // namespace ldirstat
