#include "filecategorystatsdialog.h"

#include "entrytooltip.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
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

    auto *table = new QTableWidget(static_cast<int>(items.size()), 4, this);
    table->setHorizontalHeaderLabels({tr("Color"), tr("Category"), tr("Files"), tr("Total Size")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 36);

    for (int row = 0; row < static_cast<int>(items.size()); ++row) {
        const FileCategoryCounter::Item &item = items[row];

        auto *colorItem = new QTableWidgetItem;
        colorItem->setFlags(Qt::ItemIsEnabled);
        colorItem->setBackground(themeColors.colorForFileCategory(item.category));
        colorItem->setToolTip(QString::fromUtf8(FileCategorizer::displayCategoryName(item.category)));
        table->setItem(row, 0, colorItem);

        auto *nameItem = new QTableWidgetItem(QString::fromUtf8(FileCategorizer::displayCategoryName(item.category)));
        nameItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 1, nameItem);

        auto *countItem = new QTableWidgetItem(QString::number(item.count));
        countItem->setFlags(Qt::ItemIsEnabled);
        countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 2, countItem);

        auto *sizeItem = new QTableWidgetItem(formatCategorySize(item.totalSize));
        sizeItem->setFlags(Qt::ItemIsEnabled);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 3, sizeItem);
    }

    layout->addWidget(table);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(560, 440);
}

} // namespace ldirstat
