#pragma once

#include <QWidget>

class QGridLayout;

namespace ldirstat {
class FileSystems;
}

namespace ldirstat {

class WelcomeWidget : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget* parent = nullptr);

    void populate(const FileSystems& fileSystems);

signals:
    void scanRequested(const QString& path);
    void openDirectoryRequested();

private:
    QGridLayout* fsLayout_ = nullptr;
};

} // namespace ldirstat
