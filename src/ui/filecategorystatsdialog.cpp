#include "filecategorystatsdialog.h"

#include "entrytooltip.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace ldirstat {

namespace {

QString formatCategorySize(uint64_t bytes) {
    auto formatUnit = [](double value, const char *suffix) {
        if (value < 10.0) {
            return QString::number(value, 'f', 1) + " " + suffix;
        }

        return QString::number(static_cast<uint64_t>(value)) + " " + suffix;
    };

    if (bytes < 1024) return QString::number(bytes) + " B";

    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return formatUnit(kb, "KB");

    const double mb = bytes / (1024.0 * 1024.0);
    if (mb < 1024.0) return formatUnit(mb, "MB");

    const double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return formatUnit(gb, "GB");
}

QString formatTypeLabel(FileType type) {
    if (type == FileType::Unknown) return QObject::tr("Unknown");
    if (type == FileType::Executable) return QObject::tr("Executable");
    if (type == FileType::Cache) return QObject::tr("Cache");
    if (type == FileType::VersionedSharedLibrary) return QObject::tr("Versioned Shared Library (.so.*)");

    const std::string_view extension = FileCategorizer::extensionForType(type);
    if (extension.empty()) return QObject::tr("Unknown");

    const QString extensionText = QString::fromUtf8(extension.data(), static_cast<int>(extension.size()));
    return extensionText.toUpper() + QStringLiteral(" (.%1)").arg(extensionText);
}

QWidget *createColorSwatchWidget(const QColor &color, const QString &toolTip, QWidget *parent) {
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(7, 4, 7, 4);
    layout->setSpacing(0);

    auto *swatch = new QFrame(container);
    swatch->setFixedSize(18, 11);
    swatch->setToolTip(toolTip);
    swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid palette(mid); border-radius: 2px;")
                              .arg(color.name()));
    layout->addWidget(swatch, 0, Qt::AlignCenter);

    return container;
}

void populateStatsRow(QTreeWidgetItem *item,
                      const QString &label,
                      uint64_t count,
                      uint64_t totalSize) {
    item->setFlags(Qt::ItemIsEnabled);
    item->setToolTip(0, label);
    item->setToolTip(1, label);
    item->setText(1, label);
    item->setText(2, QString::number(count));
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(3, formatCategorySize(totalSize));
    item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
}

} // namespace

FileCategoryStatsDialog::FileCategoryStatsDialog(const DirEntryStore &store,
                                                 const NameStore &names,
                                                 EntryRef dirRef,
                                                 const ThemeColors &themeColors,
                                                 QWidget *parent)
    : QDialog(parent) {
    FileCategoryCounter counter(store);
    counter.countTree(dirRef);

    std::vector<FileCategoryCounter::Item> items(counter.items().begin(), counter.items().end());
    std::sort(items.begin(), items.end(), [](const FileCategoryCounter::Item &lhs, const FileCategoryCounter::Item &rhs) {
        if (lhs.totalSize != rhs.totalSize) return lhs.totalSize > rhs.totalSize;

        return QString::fromUtf8(FileCategorizer::displayCategoryName(lhs.category))
               < QString::fromUtf8(FileCategorizer::displayCategoryName(rhs.category));
    });

    const QString directoryPath = QDir::toNativeSeparators(entryFullPath(store, names, dirRef));

    setModal(true);
    setWindowTitle(tr("File Category Statistics - %1").arg(directoryPath));

    auto *layout = new QVBoxLayout(this);

    auto *pathLabel = new QLabel(directoryPath, this);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(pathLabel);

    auto *summaryLabel = new QLabel(tr("Breakdown for the current directory and all subdirectories."), this);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *tree = new QTreeWidget(this);
    tree->setColumnCount(4);
    tree->setHeaderLabels({tr("Color"), tr("Category / File Type"), tr("Files"), tr("Total Size")});
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setFocusPolicy(Qt::NoFocus);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree->setColumnWidth(0, 36);

    const auto &typeItems = counter.typeItems();
    for (const FileCategoryCounter::Item &item : items) {
        const QString categoryLabel = QString::fromUtf8(FileCategorizer::displayCategoryName(item.category));
        const QColor categoryColor = themeColors.colorForFileCategory(item.category);

        auto *categoryItem = new QTreeWidgetItem(tree);
        populateStatsRow(categoryItem, categoryLabel, item.count, item.totalSize);
        tree->setItemWidget(categoryItem, 0, createColorSwatchWidget(categoryColor, categoryLabel, tree));

        std::vector<FileCategoryCounter::TypeItem> childItems;
        childItems.reserve(typeItems.size());
        for (const FileCategoryCounter::TypeItem &typeItem : typeItems) {
            if (typeItem.count == 0) continue;
            if (FileCategorizer::categoryForType(typeItem.type) != item.category) continue;
            childItems.push_back(typeItem);
        }

        std::sort(childItems.begin(),
                  childItems.end(),
                  [](const FileCategoryCounter::TypeItem &lhs, const FileCategoryCounter::TypeItem &rhs) {
                      if (lhs.totalSize != rhs.totalSize) return lhs.totalSize > rhs.totalSize;

                      return formatTypeLabel(lhs.type) < formatTypeLabel(rhs.type);
                  });

        for (const FileCategoryCounter::TypeItem &typeItem : childItems) {
            auto *typeRow = new QTreeWidgetItem(categoryItem);
            populateStatsRow(typeRow, formatTypeLabel(typeItem.type), typeItem.count, typeItem.totalSize);
        }
    }

    tree->collapseAll();
    layout->addWidget(tree);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(560, 440);
}

} // namespace ldirstat
