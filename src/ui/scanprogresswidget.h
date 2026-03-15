#pragma once

#include <cstdint>
#include <QWidget>

class QLabel;

namespace ldirstat {

class ScanProgressWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScanProgressWidget(QWidget* parent = nullptr);

    void reset();
    void updateCounts(uint64_t files, uint64_t dirs);

signals:
    void stopRequested();

private:
    QLabel* filesLabel_ = nullptr;
    QLabel* dirsLabel_ = nullptr;
};

} // namespace ldirstat
