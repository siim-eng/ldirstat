#include "mountlistwidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ldirstat {

MountListWidget::MountListWidget(QWidget* parent)
    : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget(scrollArea);
    listLayout_ = new QVBoxLayout(container);
    listLayout_->setContentsMargins(4, 4, 4, 4);
    listLayout_->setSpacing(6);
    listLayout_->addStretch();

    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void MountListWidget::populate(const FileSystems& fileSystems) {
    // Remove all items except the trailing stretch.
    while (listLayout_->count() > 1) {
        auto* item = listLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }

    for (const auto& m : fileSystems.mounts()) {
        if (m.kind != FileSystemType::Real && m.kind != FileSystemType::Network)
            continue;

        auto* row = new QHBoxLayout();

        double freePct = m.totalBytes > 0
            ? (static_cast<double>(m.availBytes) / m.totalBytes) * 100.0
            : 0.0;

        auto formatSize = [](uint64_t bytes) -> QString {
            if (bytes < 1024) return QString::number(bytes) + " B";
            double kb = bytes / 1024.0;
            if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
            double mb = kb / 1024.0;
            if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
            double gb = mb / 1024.0;
            return QString::number(gb, 'f', 1) + " GB";
        };

        QString text = QString("%3 - %1\n%2 on %3\n%4 free of %5 (%6% free)")
            .arg(QString::fromStdString(m.fsType))
            .arg(QString::fromStdString(m.device))
            .arg(QString::fromStdString(m.mountPoint))
            .arg(formatSize(m.availBytes))
            .arg(formatSize(m.totalBytes))
            .arg(QString::number(freePct, 'f', 0));

        auto* label = new QLabel(text);
        label->setWordWrap(true);
        row->addWidget(label, 1);

        auto* scanBtn = new QPushButton(tr("Scan"));
        scanBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto mountPoint = QString::fromStdString(m.mountPoint);
        connect(scanBtn, &QPushButton::clicked, this, [this, mountPoint]() {
            emit scanRequested(mountPoint);
        });
        row->addWidget(scanBtn, 0, Qt::AlignTop);

        auto* rowWidget = new QWidget();
        rowWidget->setLayout(row);
        listLayout_->insertWidget(listLayout_->count() - 1, rowWidget);
    }
}

} // namespace ldirstat
