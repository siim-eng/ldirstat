#include "welcomewidget.h"
#include "filesystem.h"

#include <QDir>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace ldirstat {

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget(scrollArea);
    auto* layout = new QVBoxLayout(container);
    layout->setAlignment(Qt::AlignHCenter);

    layout->addStretch();

    // Title.
    auto* title = new QLabel(tr("LDirStat"));
    QFont titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    // Subtitle.
    auto* subtitle = new QLabel(tr("LDirStat analyzes disk usage statistics so you can see what is using lots of space."));
    QFont subtitleFont = subtitle->font();
    subtitleFont.setPointSize(14);
    subtitle->setFont(subtitleFont);
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(24);

    // Action buttons row.
    auto* actionRow = new QHBoxLayout();
    actionRow->setAlignment(Qt::AlignCenter);

    QFont actionFont = font();
    actionFont.setPointSize(12);
    QSize iconSize(32, 32);

    auto makeButton = [&](const QString& text,
                          QStyle::StandardPixmap pixmap) {
        auto* btn = new QPushButton(text);
        btn->setFont(actionFont);
        btn->setIcon(style()->standardIcon(pixmap));
        btn->setIconSize(iconSize);
        btn->setMinimumHeight(60);
        btn->setMinimumWidth(180);
        return btn;
    };

    auto* homeBtn = makeButton(tr("Open Home"), QStyle::SP_DirHomeIcon);
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit scanRequested(QDir::homePath());
    });
    actionRow->addWidget(homeBtn);

    auto* rootBtn = makeButton(tr("Open Root"), QStyle::SP_DirOpenIcon);
    connect(rootBtn, &QPushButton::clicked, this, [this]() {
        emit scanRequested(QStringLiteral("/"));
    });
    actionRow->addWidget(rootBtn);

    auto* otherBtn = makeButton(tr("Open Other Directory..."), QStyle::SP_DirIcon);
    connect(otherBtn, &QPushButton::clicked, this, [this]() {
        emit openDirectoryRequested();
    });
    actionRow->addWidget(otherBtn);

    layout->addLayout(actionRow);

    layout->addSpacing(24);

    // Filesystems section header.
    auto* fsHeader = new QLabel(tr("Open a File System"));
    QFont headerFont = fsHeader->font();
    headerFont.setPointSize(12);
    headerFont.setBold(true);
    fsHeader->setFont(headerFont);
    fsHeader->setAlignment(Qt::AlignCenter);
    layout->addWidget(fsHeader);

    layout->addSpacing(8);

    // Filesystem buttons grid (wraps at 3 columns).
    fsLayout_ = new QGridLayout();
    fsLayout_->setAlignment(Qt::AlignCenter);
    layout->addLayout(fsLayout_);

    layout->addStretch();

    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void WelcomeWidget::populate(const FileSystems& fileSystems) {
    // Clear existing buttons.
    while (fsLayout_->count() > 0) {
        auto* item = fsLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }

    constexpr int columns = 3;
    int idx = 0;

    auto formatSize = [](uint64_t bytes) -> QString {
        if (bytes < 1024) return QString::number(bytes) + " B";
        double kb = bytes / 1024.0;
        if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
        double mb = kb / 1024.0;
        if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
        double gb = mb / 1024.0;
        return QString::number(gb, 'f', 1) + " GB";
    };

    QFont fsFont = font();
    fsFont.setPointSize(10);
    QSize fsIconSize(32, 32);

    for (const auto& m : fileSystems.mounts()) {
        if (m.kind != FileSystemType::Real && m.kind != FileSystemType::Network)
            continue;

        double freePct = m.totalBytes > 0
            ? (static_cast<double>(m.availBytes) / m.totalBytes) * 100.0
            : 0.0;

        QString text = QString("%1\n%2 -> %3\n%4 free of %5 (%6% free)")
            .arg(QString::fromStdString(m.fsType))
            .arg(QString::fromStdString(m.device))
            .arg(QString::fromStdString(m.mountPoint))
            .arg(formatSize(m.availBytes))
            .arg(formatSize(m.totalBytes))
            .arg(QString::number(freePct, 'f', 0));

        auto* btn = new QPushButton(text);
        btn->setFont(fsFont);
        btn->setMinimumHeight(60);
        btn->setMinimumWidth(200);

        auto pixmap = (m.kind == FileSystemType::Network)
            ? QStyle::SP_DriveNetIcon
            : QStyle::SP_DriveHDIcon;
        btn->setIcon(style()->standardIcon(pixmap));
        btn->setIconSize(fsIconSize);

        auto mountPoint = QString::fromStdString(m.mountPoint);
        connect(btn, &QPushButton::clicked, this, [this, mountPoint]() {
            emit scanRequested(mountPoint);
        });

        fsLayout_->addWidget(btn, idx / columns, idx % columns);
        ++idx;
    }
}

} // namespace ldirstat
