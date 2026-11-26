#include "RawRecoveryDialog.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "CsvCodec.hpp"

#if defined(AUTO_VERSION) && AUTO_VERSION
#include "version.h"
#else
#pragma message("AUTO_VERSION is disabled, using default major version 0 and minor version 9")
#undef APP_VERSION_MAJOR
#undef APP_VERSION_MINOR
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 9
#endif

namespace b210post {
RawRecoveryDialog::RawRecoveryDialog(const EnuReference& f, QWidget* p) : QDialog(p), fallback_(f) {
    setWindowTitle("Recover Type-A from Raw Timeline");
    resize(980, 620);
    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    path_ = new QLineEdit;
    path_->setReadOnly(true);
    auto csv_version = QStringLiteral("V%1.%2").arg(APP_VERSION_MAJOR).arg(APP_VERSION_MINOR);
    path_->setToolTip(QStringLiteral("Selected >=%1 raw_timeline.csv path.").arg(csv_version));
    auto* browse = new QPushButton("Browse...");
    browse->setToolTip(QStringLiteral("Choose a raw_timeline.csv written continuously by B210RadioMap %1.").arg(csv_version));
    auto* loadb = new QPushButton("Load and Recover");
    loadb->setToolTip(
        "Parse the timeline and recover only RF points confirmed by RF_ACCEPTED with a valid "
        "adjacent GNSS bracket.");
    row->addWidget(new QLabel("Raw timeline:"));
    row->addWidget(path_, 1);
    row->addWidget(browse);
    row->addWidget(loadb);
    root->addLayout(row);
    summary_ = new QLabel("No file loaded.");
    summary_->setWordWrap(true);
    summary_->setToolTip(
        QStringLiteral("Recovery counts and warnings. TX markers cannot be recovered because %1 does not record them in the raw timeline.").arg(csv_version)
    );
    root->addWidget(summary_);
    table_ = new QTableWidget;
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(
    {"RF Seq", "Segment", "Time [ns]", "East [m]", "North [m]", "Power [dBFS]", "Geo Status"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setToolTip(
        "Preview of recovered Type-A RX points. Synthetic WGS84 is used only when real geographic "
        "coordinates are unavailable.");
    root->addWidget(table_, 1);
    save_ = new QPushButton("Export Type-A CSV...");
    save_->setEnabled(false);
    save_->setToolTip(
        "Save the recovered points using the V0.9-compatible Type-A CSV format. TX markers are not "
        "present in raw_timeline.csv.");
    auto* close = new QPushButton("Close");
    close->setToolTip("Close the recovery window.");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(save_);
    buttons->addWidget(close);
    root->addLayout(buttons);
    connect(browse, &QPushButton::clicked, this, &RawRecoveryDialog::choose);
    connect(loadb, &QPushButton::clicked, this, &RawRecoveryDialog::load);
    connect(save_, &QPushButton::clicked, this, &RawRecoveryDialog::save);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void RawRecoveryDialog::choose() {
    auto p = QFileDialog::getOpenFileName(this, "Open Raw Timeline", {}, "CSV (*.csv)");
    if (!p.isEmpty())
        path_->setText(p);
}

void RawRecoveryDialog::load() {
    if (path_->text().isEmpty())
        return;
    auto r = CsvCodec::recoverRawTimeline(path_->text(), fallback_);
    if (!r.report.ok()) {
        QMessageBox::warning(this, "Recovery Failed", r.report.errors.join('\n'));
        return;
    }
    recovered_ = std::move(r.dataset);
    table_->setRowCount(int(recovered_.points.size()));
    for (int i = 0; i < table_->rowCount(); ++i) {
        auto& p = recovered_.points[size_t(i)];
        QStringList v = {QString::number(qulonglong(p.rf_sequence)),
                         QString::number(qulonglong(p.segment_id)),
                         QString::number(qlonglong(p.timestamp_ns)),
                         QString::number(p.east_m, 'f', 3),
                         QString::number(p.north_m, 'f', 3),
                         QString::number(p.power_dbfs, 'f', 3),
                         geoStatusName(p.geo_status)
                        };
        for (int c = 0; c < v.size(); ++c)
            table_->setItem(i, c, new QTableWidgetItem(v[c]));
    }
    QString s = QString(
                    "Raw RF: %1 | Confirmed recovered: %2 | Rejected/unconfirmed: %3. TX markers "
                    "cannot be recovered from >=V0.9 raw timelines.")
                .arg(r.raw_rf_rows)
                .arg(r.confirmed_points)
                .arg(r.rejected_rows);
    if (!r.report.warnings.isEmpty())
        s += "\nWarnings: " + r.report.warnings.join("; ");
    summary_->setText(s);
    save_->setEnabled(!recovered_.points.empty());
}

void RawRecoveryDialog::save() {
    auto p = QFileDialog::getSaveFileName(this, "Export Recovered Type-A", {}, "CSV (*.csv)");
    if (p.isEmpty())
        return;
    const QFileInfo source(path_->text());
    const QFileInfo destination(p);
    const QString sourcePath =
        source.canonicalFilePath().isEmpty() ? source.absoluteFilePath() : source.canonicalFilePath();
    const QString destinationPath = destination.canonicalFilePath().isEmpty()
                                    ? destination.absoluteFilePath()
                                    : destination.canonicalFilePath();
#ifdef Q_OS_WIN
    const bool samePath = sourcePath.compare(destinationPath, Qt::CaseInsensitive) == 0;
#else
    const bool samePath = sourcePath == destinationPath;
#endif
    if (samePath) {
        QMessageBox::warning(
            this, "Raw Source Is Read-only",
            "The recovered table lives in memory. Choose a different path; the imported raw "
            "timeline can never be overwritten.");
        return;
    }
    QString e;
    if (!CsvCodec::saveRadio(p, recovered_, &e))
        QMessageBox::warning(this, "Export Failed", e);
    else
        QMessageBox::information(this, "Export Complete", "Recovered Type-A CSV saved successfully.");
}
}  // namespace b210post
