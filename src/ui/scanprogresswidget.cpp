#include "scanprogresswidget.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace ldirstat {

ScanProgressWidget::ScanProgressWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addStretch();

    auto* title = new QLabel(tr("Scanning..."), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(16);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* progressBar = new QProgressBar(this);
    progressBar->setMinimum(0);
    progressBar->setMaximum(0);
    layout->addWidget(progressBar);

    filesLabel_ = new QLabel(this);
    filesLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(filesLabel_);

    dirsLabel_ = new QLabel(this);
    dirsLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(dirsLabel_);

    layout->addSpacing(8);

    auto* stopButton = new QPushButton(tr("Stop"), this);
    stopButton->setFixedWidth(120);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(stopButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    layout->addStretch();

    connect(stopButton, &QPushButton::clicked, this, &ScanProgressWidget::stopRequested);
}

void ScanProgressWidget::reset() {
    filesLabel_->setText(tr("Files: 0"));
    dirsLabel_->setText(tr("Directories: 0"));
}

void ScanProgressWidget::updateCounts(uint64_t files, uint64_t dirs) {
    filesLabel_->setText(tr("Files: %1").arg(files));
    dirsLabel_->setText(tr("Directories: %1").arg(dirs));
}

} // namespace ldirstat
