#include "welcomewidget.h"
#include "filesystem.h"
#include "iconutil.h"

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

namespace {

QString formatSize(uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";

    double kb = bytes / 1024.0;
    if (kb < 1024.0) return QString::number(kb, 'f', 1) + " KB";

    double mb = kb / 1024.0;
    if (mb < 1024.0) return QString::number(mb, 'f', 1) + " MB";

    double gb = mb / 1024.0;
    if (gb < 1024.0) return QString::number(gb, 'f', 1) + " GB";

    const double tb = gb / 1024.0;
    return QString::number(tb, 'f', 1) + " TB";
}

QString volumeTitle(const VolumeInfo &volume) {
    const QString label = QString::fromStdString(volume.label);
    const QString fsType = QString::fromStdString(volume.fsType);
    if (!label.isEmpty()) return QString("%1 (%2)").arg(label, fsType);
    return fsType;
}

QIcon volumeIcon(const QWidget *widget, const VolumeInfo &volume) {
    if (volume.kind == FileSystemType::Network)
        return themedIcon(widget, QStringLiteral("network-server-symbolic"), QStyle::SP_DriveNetIcon);
    if (volume.removable || volume.hotplug)
        return themedIcon(widget, QStringLiteral("drive-removable-media-symbolic"), QStyle::SP_DriveFDIcon);
    return themedIcon(widget, QStringLiteral("drive-harddisk-symbolic"), QStyle::SP_DriveHDIcon);
}

} // namespace

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(container);
    layout->setAlignment(Qt::AlignHCenter);

    layout->addStretch();

    auto *title = new QLabel(tr("Devices and Locations"));
    QFont titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *subtitle =
        new QLabel(tr("LDirStat analyzes disk usage statistics so you can see what is using lots of space."));
    QFont subtitleFont = subtitle->font();
    subtitleFont.setPointSize(14);
    subtitle->setFont(subtitleFont);
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(24);

    auto *actionRow = new QHBoxLayout();
    actionRow->setAlignment(Qt::AlignCenter);

    QFont actionFont = font();
    actionFont.setPointSize(12);
    const QSize iconSize(32, 32);

    auto makeButton = [&](const QString &text, const QString &themeName, QStyle::StandardPixmap fallback) {
        auto *btn = new QPushButton(text);
        btn->setFont(actionFont);
        btn->setIcon(themedIcon(this, themeName, fallback));
        btn->setIconSize(iconSize);
        btn->setMinimumHeight(60);
        btn->setMinimumWidth(180);
        return btn;
    };

    auto *homeBtn = makeButton(tr("Open Home"), QStringLiteral("go-home-symbolic"), QStyle::SP_DirHomeIcon);
    connect(homeBtn, &QPushButton::clicked, this, [this]() { emit scanRequested(QDir::homePath()); });
    actionRow->addWidget(homeBtn);

    auto *rootBtn =
        makeButton(tr("Open Root"), QStringLiteral("drive-harddisk-system-symbolic"), QStyle::SP_DirOpenIcon);
    connect(rootBtn, &QPushButton::clicked, this, [this]() { emit scanRequested(QStringLiteral("/")); });
    actionRow->addWidget(rootBtn);

    auto *otherBtn =
        makeButton(tr("Open Other Directory..."), QStringLiteral("folder-open-symbolic"), QStyle::SP_DirIcon);
    connect(otherBtn, &QPushButton::clicked, this, [this]() { emit openDirectoryRequested(); });
    actionRow->addWidget(otherBtn);

    layout->addLayout(actionRow);

    layout->addSpacing(24);

    auto *fsHeader = new QLabel(tr("Open a File System"));
    QFont headerFont = fsHeader->font();
    headerFont.setPointSize(12);
    headerFont.setBold(true);
    fsHeader->setFont(headerFont);
    fsHeader->setAlignment(Qt::AlignCenter);
    layout->addWidget(fsHeader);

    layout->addSpacing(8);

    statusLabel_ = new QLabel(container);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setVisible(false);
    layout->addWidget(statusLabel_);

    layout->addSpacing(8);

    fsLayout_ = new QGridLayout();
    fsLayout_->setAlignment(Qt::AlignCenter);
    layout->addLayout(fsLayout_);

    layout->addStretch();

    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void WelcomeWidget::populate(const FileSystems &fileSystems) {
    while (auto *item = fsLayout_->takeAt(0)) {
        if (auto *widget = item->widget()) delete widget;
        delete item;
    }

    constexpr int columns = 3;
    int idx = 0;

    QFont fsFont = font();
    fsFont.setPointSize(10);
    const QSize fsIconSize(32, 32);

    for (const auto &volume : fileSystems.volumes()) {
        if (volume.kind != FileSystemType::Real && volume.kind != FileSystemType::Network) {
            continue;
        }

        QString text;
        if (volume.mounted) {
            const double freePct =
                volume.totalBytes > 0 ? (static_cast<double>(volume.availBytes) / volume.totalBytes) * 100.0 : 0.0;
            const QString capacityLine = volume.totalBytes > 0 ? QString("%1 free of %2 (%3% free)")
                                                                     .arg(formatSize(volume.availBytes))
                                                                     .arg(formatSize(volume.totalBytes))
                                                                     .arg(QString::number(freePct, 'f', 0))
                                                               : tr("Mounted");

            text = QString("%1\n%2\n%3\n%4")
                       .arg(volumeTitle(volume))
                       .arg(QString::fromStdString(volume.devicePath))
                       .arg(QString::fromStdString(volume.mountPoint))
                       .arg(capacityLine);
        } else {
            QString stateLine = tr("Not mounted");
            if (volume.readOnly) stateLine += tr(" (read-only)");

            const QString sizeLine = volume.sizeBytes > 0 ? formatSize(volume.sizeBytes) : tr("Size unavailable");

            text = QString("%1\n%2\n%3\n%4")
                       .arg(volumeTitle(volume))
                       .arg(QString::fromStdString(volume.devicePath))
                       .arg(stateLine)
                       .arg(sizeLine);
        }

        auto *btn = new QPushButton(text);
        btn->setFont(fsFont);
        btn->setMinimumHeight(90);
        btn->setMinimumWidth(220);
        btn->setIcon(volumeIcon(this, volume));
        btn->setIconSize(fsIconSize);

        if (volume.mounted) {
            const QString mountPoint = QString::fromStdString(volume.mountPoint);
            connect(btn, &QPushButton::clicked, this, [this, mountPoint]() { emit scanRequested(mountPoint); });
        } else {
            const QString devicePath = QString::fromStdString(volume.devicePath);
            connect(btn, &QPushButton::clicked, this, [this, devicePath]() { emit mountAndScanRequested(devicePath); });
        }

        fsLayout_->addWidget(btn, idx / columns, idx % columns);
        ++idx;
    }

    setBusy(busy_, busyStatus_);
}

void WelcomeWidget::setBusy(bool busy, const QString &status) {
    busy_ = busy;
    busyStatus_ = status;

    statusLabel_->setText(status);
    statusLabel_->setVisible(busy_ && !busyStatus_.isEmpty());

    for (auto *button : findChildren<QPushButton *>())
        button->setEnabled(!busy_);
}

} // namespace ldirstat
