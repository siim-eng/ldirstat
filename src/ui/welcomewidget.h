#pragma once

#include <QWidget>

class QGridLayout;
class QLabel;

namespace ldirstat {

class FileSystems;

class WelcomeWidget : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

    void populate(const FileSystems &fileSystems);
    void setBusy(bool busy, const QString &status = {});

signals:
    void scanRequested(const QString &path);
    void mountAndScanRequested(const QString &devicePath);
    void openDirectoryRequested();

private:
    QGridLayout *fsLayout_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    bool busy_ = false;
    QString busyStatus_;
};

} // namespace ldirstat
