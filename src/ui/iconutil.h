#pragma once

#include <QIcon>
#include <QString>
#include <QStyle>
#include <QWidget>

namespace ldirstat {

inline QIcon themedIcon(const QWidget *widget, const QString &themeName, QStyle::StandardPixmap fallback) {
    return QIcon::fromTheme(themeName, widget->style()->standardIcon(fallback));
}

} // namespace ldirstat
