#pragma once

#include <QWidget>

#include "filesystem.h"

class QVBoxLayout;

namespace ldirstat {

class MountListWidget : public QWidget {
    Q_OBJECT

public:
    explicit MountListWidget(QWidget* parent = nullptr);

    void populate(const FileSystems& fileSystems);

signals:
    void scanRequested(const QString& mountPoint);

private:
    QVBoxLayout* listLayout_ = nullptr;
};

} // namespace ldirstat
