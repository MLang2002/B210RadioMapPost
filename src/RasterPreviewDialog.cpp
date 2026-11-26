#include "RasterPreviewDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace b210post {
namespace {

QString pngPath(QString path) {
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    return path;
}

QDoubleSpinBox* doubleSpin(double minimum, double maximum, double value, int decimals,
                           const QString& tooltip) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setToolTip(tooltip);
    return spin;
}

QSpinBox* integerSpin(int minimum, int maximum, int value, const QString& tooltip) {
    auto* spin = new QSpinBox;
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setToolTip(tooltip);
    return spin;
}

bool editCompletionSettings(RasterCompletionSettings* settings, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Raster Cell Completion Settings"));
    dialog.setMinimumWidth(520);
    auto* method = new QComboBox;
    method->addItem(QStringLiteral("Raster IDW"), int(RasterCompletionMethod::Idw));
    method->addItem(QStringLiteral("Local Ordinary Kriging"),
                    int(RasterCompletionMethod::OrdinaryKriging));
    method->setCurrentIndex(method->findData(int(settings->method)));
    method->setToolTip(
        "Choose the method used only for non-building raster cells with sample_count equal to 0.");
    auto* neighbors = integerSpin(1, 64, settings->neighbor_count,
                                  "Maximum measured raster cells used per missing cell.");
    auto* minimum = integerSpin(1, 64, settings->minimum_neighbors,
                                "Minimum usable measured raster cells required.");
    auto* quadrants = integerSpin(1, 4, settings->minimum_quadrants,
                                  "Required occupied quadrants around the target cell.");
    auto* maximumDistance =
        doubleSpin(0.0, 100000.0, settings->maximum_distance_m, 3,
                   "Maximum source distance in metres. Set 0 for unlimited distance.");
    auto* exponent = doubleSpin(0.1, 10.0, settings->idw_exponent, 2,
                                "IDW exponent; larger values emphasize nearby cells.");
    auto* rejectBuilding = new QCheckBox(QStringLiteral("Reject building-crossing paths"));
    rejectBuilding->setChecked(settings->reject_building_crossing);
    rejectBuilding->setToolTip(
        "Reject a source cell when the source-target raster line crosses the building mask.");
    auto* fallback = new QCheckBox(QStringLiteral("Use nearest-cell fallback"));
    fallback->setChecked(settings->nearest_fallback);
    fallback->setToolTip(
        "If the primary method cannot fill a cell, copy the nearest eligible measured cell.");
    auto* fallbackDistance =
        doubleSpin(0.0, 100000.0, settings->fallback_maximum_distance_m, 3,
                   "Maximum nearest-fallback distance in metres. Set 0 for unlimited distance.");
    auto* variogram = new QComboBox;
    variogram->addItem(QStringLiteral("Spherical"), QStringLiteral("spherical"));
    variogram->addItem(QStringLiteral("Exponential"), QStringLiteral("exponential"));
    variogram->addItem(QStringLiteral("Gaussian"), QStringLiteral("gaussian"));
    variogram->setCurrentIndex(variogram->findData(settings->variogram_model));
    variogram->setToolTip("Semivariogram model used by ordinary kriging.");
    auto* nugget =
        doubleSpin(0.0, 10000.0, settings->nugget_db2, 4, "Kriging nugget in squared-dB units.");
    auto* sill = doubleSpin(0.0001, 10000.0, settings->partial_sill_db2, 4,
                            "Kriging partial sill in squared-dB units.");
    auto* range =
        doubleSpin(0.001, 100000.0, settings->range_m, 3, "Kriging correlation range in metres.");
    auto* regularization = doubleSpin(0.0, 100.0, settings->regularization, 6,
                                      "Diagonal stabilization for kriging systems.");
    auto* maximumVariance = doubleSpin(0.0, 100000.0, settings->maximum_variance_db2, 3,
                                       "Reject kriging predictions above this squared-dB variance.");

    auto updateMethodControls = [=] {
        const bool kriging =
        method->currentData().toInt() == int(RasterCompletionMethod::OrdinaryKriging);
        exponent->setEnabled(!kriging);
        variogram->setEnabled(kriging);
        nugget->setEnabled(kriging);
        sill->setEnabled(kriging);
        range->setEnabled(kriging);
        regularization->setEnabled(kriging);
        maximumVariance->setEnabled(kriging);
    };
    QObject::connect(method, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [=](int) {
        updateMethodControls();
    });
    updateMethodControls();

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Method"), method);
    form->addRow(QStringLiteral("Neighbor count"), neighbors);
    form->addRow(QStringLiteral("Minimum neighbors"), minimum);
    form->addRow(QStringLiteral("Minimum quadrants"), quadrants);
    form->addRow(QStringLiteral("Maximum distance [m]"), maximumDistance);
    form->addRow(QStringLiteral("IDW exponent"), exponent);
    form->addRow(QString(), rejectBuilding);
    form->addRow(QString(), fallback);
    form->addRow(QStringLiteral("Fallback distance [m]"), fallbackDistance);
    form->addRow(QStringLiteral("Variogram"), variogram);
    form->addRow(QStringLiteral("Nugget [dB²]"), nugget);
    form->addRow(QStringLiteral("Partial sill [dB²]"), sill);
    form->addRow(QStringLiteral("Range [m]"), range);
    form->addRow(QStringLiteral("Regularization"), regularization);
    form->addRow(QStringLiteral("Maximum variance [dB²]"), maximumVariance);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)
    ->setToolTip("Apply these settings to the Preview window only.");
    buttons->button(QDialogButtonBox::Cancel)
    ->setToolTip("Discard changes to Raster Cell Completion settings.");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* root = new QVBoxLayout(&dialog);
    root->addLayout(form);
    root->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    RasterCompletionSettings candidate = *settings;
    candidate.method = static_cast<RasterCompletionMethod>(method->currentData().toInt());
    candidate.neighbor_count = neighbors->value();
    candidate.minimum_neighbors = minimum->value();
    candidate.minimum_quadrants = quadrants->value();
    candidate.maximum_distance_m = maximumDistance->value();
    candidate.idw_exponent = exponent->value();
    candidate.reject_building_crossing = rejectBuilding->isChecked();
    candidate.nearest_fallback = fallback->isChecked();
    candidate.fallback_maximum_distance_m = fallbackDistance->value();
    candidate.variogram_model = variogram->currentData().toString();
    candidate.nugget_db2 = nugget->value();
    candidate.partial_sill_db2 = sill->value();
    candidate.range_m = range->value();
    candidate.regularization = regularization->value();
    candidate.maximum_variance_db2 = maximumVariance->value();
    QString error;
    if (!RasterCompletion::validate(candidate, &error)) {
        QMessageBox::warning(parent, QStringLiteral("Invalid Completion Settings"), error);
        return false;
    }
    *settings = candidate;
    return true;
}

bool editInpaintingSettings(RasterInpaintingSettings* settings, bool hasSelectedMissing,
                            QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Assisted Progressive Fill Settings"));
    dialog.setMinimumWidth(520);
    auto* allComponents = new QCheckBox(QStringLiteral("Fill all currently missing components"));
    allComponents->setChecked(settings->fill_all_components || !hasSelectedMissing);
    allComponents->setEnabled(hasSelectedMissing);
    allComponents->setToolTip(
        "Unchecked fills the missing component under the last inspected pixel. Checked fills all "
        "currently missing non-building components.");
    auto* radius = integerSpin(1, 64, settings->neighborhood_radius_px,
                               "Local source-search radius in raster pixels.");
    auto* minimum = integerSpin(1, 128, settings->minimum_sources,
                                "Minimum local non-building sources required for a prediction.");
    auto* exponent = doubleSpin(0.1, 10.0, settings->distance_exponent, 2,
                                "Distance exponent used by the weighted local trend fit.");
    auto* decay = doubleSpin(0.1, 1000.0, settings->confidence_decay_layers, 2,
                             "Confidence decay length measured in progressive fill layers.");
    auto* minimumConfidence =
        doubleSpin(0.0, 1.0, settings->minimum_confidence, 3,
                   "Stop filling a cell when its predicted confidence is below this value.");
    auto* maximumLayers = integerSpin(1, 10000, settings->maximum_layers,
                                      "Maximum number of synchronized edge-to-center layers.");
    auto* margin = doubleSpin(0.0, 100.0, settings->clamp_margin_db, 2,
                              "Allowed extension beyond robust local power quantiles in dB.");
    auto* eightConnected = new QCheckBox(QStringLiteral("Use 8-connected missing components"));
    eightConnected->setChecked(settings->use_eight_connectivity);
    eightConnected->setToolTip(
        "Treat diagonally touching missing pixels as part of the same component.");
    auto* form = new QFormLayout;
    form->addRow(QString(), allComponents);
    form->addRow(QStringLiteral("Neighborhood radius [px]"), radius);
    form->addRow(QStringLiteral("Minimum sources"), minimum);
    form->addRow(QStringLiteral("Distance exponent"), exponent);
    form->addRow(QStringLiteral("Confidence decay [layers]"), decay);
    form->addRow(QStringLiteral("Minimum confidence"), minimumConfidence);
    form->addRow(QStringLiteral("Maximum layers"), maximumLayers);
    form->addRow(QStringLiteral("Power clamp margin [dB]"), margin);
    form->addRow(QString(), eightConnected);
    auto* explanation = new QLabel(
        QStringLiteral("The fill advances synchronously from each missing-region edge toward its "
                       "center. A weighted local power plane preserves the nearby propagation "
                       "gradient; generated values receive decaying confidence."));
    explanation->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)
    ->setToolTip("Calculate the proposal and open its result preview.");
    buttons->button(QDialogButtonBox::Cancel)->setToolTip("Cancel without changing raster values.");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* root = new QVBoxLayout(&dialog);
    root->addWidget(explanation);
    root->addLayout(form);
    root->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    RasterInpaintingSettings candidate = *settings;
    candidate.fill_all_components = allComponents->isChecked() || !hasSelectedMissing;
    candidate.neighborhood_radius_px = radius->value();
    candidate.minimum_sources = minimum->value();
    candidate.distance_exponent = exponent->value();
    candidate.confidence_decay_layers = decay->value();
    candidate.minimum_confidence = minimumConfidence->value();
    candidate.maximum_layers = maximumLayers->value();
    candidate.clamp_margin_db = margin->value();
    candidate.use_eight_connectivity = eightConnected->isChecked();
    QString error;
    if (!RasterInpainting::validate(candidate, &error)) {
        QMessageBox::warning(parent, QStringLiteral("Invalid Assisted Fill Settings"), error);
        return false;
    }
    *settings = candidate;
    return true;
}

}  // namespace

class RasterImageView final : public QGraphicsView {
  public:
    explicit RasterImageView(QWidget* parent = nullptr) : QGraphicsView(parent), scene_(this) {
        setScene(&scene_);
        setMouseTracking(true);
        setDragMode(QGraphicsView::NoDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setToolTip(
            "Wheel zooms. Hover inspects a raster cell. In Edit Pixels mode, left-click writes the "
            "configured power and right-click removes the manual override.");
    }

    void setImage(const QImage& image) {
        imageSize_ = image.size();
        if (!item_) {
            item_ = scene_.addPixmap(QPixmap::fromImage(image));
            item_->setTransformationMode(Qt::FastTransformation);
            scene_.setSceneRect(QRectF(QPointF(0.0, 0.0), QSizeF(image.size())));
            fitInView(scene_.sceneRect(), Qt::KeepAspectRatio);
        } else {
            item_->setPixmap(QPixmap::fromImage(image));
            scene_.setSceneRect(QRectF(QPointF(0.0, 0.0), QSizeF(image.size())));
        }
    }

    void setOverlay(const QImage& image, bool visible) {
        if (!overlayItem_) {
            overlayItem_ = scene_.addPixmap(QPixmap::fromImage(image));
            overlayItem_->setTransformationMode(Qt::FastTransformation);
            overlayItem_->setZValue(1.0);
        } else {
            overlayItem_->setPixmap(QPixmap::fromImage(image));
        }
        overlayItem_->setVisible(visible);
    }

    void setOverlayVisible(bool visible) {
        if (overlayItem_) {
            overlayItem_->setVisible(visible);
        }
    }

    std::function<void(int, int)> hover_callback;
    std::function<void(int, int, Qt::MouseButton)> click_callback;

  protected:
    void wheelEvent(QWheelEvent* event) override {
        const qreal factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        scale(factor, factor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPointF scenePoint = mapToScene(event->pos());
        const int x = static_cast<int>(std::floor(scenePoint.x()));
        const int y = static_cast<int>(std::floor(scenePoint.y()));
        if (hover_callback && x >= 0 && y >= 0 && x < imageSize_.width() && y < imageSize_.height()) {
            hover_callback(x, y);
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        const QPointF scenePoint = mapToScene(event->pos());
        const int x = static_cast<int>(std::floor(scenePoint.x()));
        const int y = static_cast<int>(std::floor(scenePoint.y()));
        if (click_callback && x >= 0 && y >= 0 && x < imageSize_.width() && y < imageSize_.height()) {
            click_callback(x, y, event->button());
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

  private:
    QGraphicsScene scene_;
    QGraphicsPixmapItem* item_ = nullptr;
    QGraphicsPixmapItem* overlayItem_ = nullptr;
    QSize imageSize_;
};

RasterPreviewDialog::RasterPreviewDialog(RenderBundle bundle, int imageType, QString imageKind,
        QJsonObject metadata, QWidget* parent)
    : QDialog(parent),
      bundle_(std::move(bundle)),
      imageType_(imageType),
      imageKind_(std::move(imageKind)),
      metadata_(std::move(metadata)) {
    setWindowTitle(QStringLiteral("Render Preview - ") + imageKind_);
    resize(1050, 820);
    view_ = new RasterImageView;
    view_->hover_callback = [this](int x, int y) {
        inspectPixel(x, y);
    };
    view_->click_callback = [this](int x, int y, Qt::MouseButton button) {
        editPixel(x, y, button);
    };

    fillMissing_ = new QCheckBox(QStringLiteral("Fill missing raster cells"));
    fillMissing_->setToolTip(
        "Complete only cells where sample_count is 0 and the building mask is 0.");
    auto* fillSettings = new QPushButton(QStringLiteral("Fill Settings..."));
    fillSettings->setToolTip("Configure Preview-only raster IDW or ordinary-kriging parameters.");
    auto* applyFill = new QPushButton(QStringLiteral("Apply / Refresh Fill"));
    applyFill->setToolTip(
        "Recalculate completion from measured raster cells; previous interpolated cells are never "
        "used as new sources.");
    showMissing_ = new QCheckBox(QStringLiteral("Show missing overlay"));
    showMissing_->setChecked(true);
    showMissing_->setToolTip(
        "Show the selected missing-cell scope as a Preview-only overlay; it is never exported.");
    blinkMissingButton_ = new QPushButton(QStringLiteral("Blink Missing"));
    blinkMissingButton_->setCheckable(true);
    blinkMissingButton_->setToolTip(
        "Flash the selected missing-cell scope. Click again to stop; Preview overlays are never "
        "exported.");
    currentMissingOnly_ = new QCheckBox(QStringLiteral("Current missing only"));
    currentMissingOnly_->setChecked(true);
    currentMissingOnly_->setToolTip(
        "Checked highlights only unresolved cells. Unchecked highlights every free-space cell "
        "that was missing before Preview completion.");

    editPixels_ = new QCheckBox(QStringLiteral("Edit pixels"));
    editPixels_->setToolTip(
        "Enable left-click power assignment and right-click manual-override removal.");
    editPower_ = doubleSpin(-300.0, 100.0, bundle_.default_power_dbfs, 3,
                            "Power written by the pixel editor in dBFS.");
    brushSize_ = new QComboBox;
    brushSize_->addItem(QStringLiteral("1 × 1"), 1);
    brushSize_->addItem(QStringLiteral("3 × 3"), 3);
    brushSize_->addItem(QStringLiteral("5 × 5"), 5);
    brushSize_->setToolTip("Square pixel-edit brush size.");
    auto* suggestButton = new QPushButton(QStringLiteral("Suggest Power"));
    suggestButton->setToolTip(
        "Estimate power at the last inspected non-building pixel and copy it into Power [dBFS] "
        "without changing the raster.");
    auto* assistedFillButton = new QPushButton(QStringLiteral("Assisted Fill..."));
    assistedFillButton->setToolTip(
        "Fill the inspected missing component, or all remaining components, by synchronized "
        "direction-aware edge-to-center progression.");
    unlockMeasured_ = new QCheckBox(QStringLiteral("Unlock measured cells"));
    unlockMeasured_->setToolTip(
        "Allow manual overrides of cells containing aggregated Type-A points. Building cells "
        "remain locked.");
    undoButton_ = new QPushButton(QStringLiteral("Undo"));
    undoButton_->setToolTip("Undo the most recent pixel-edit brush or assisted-fill operation.");
    redoButton_ = new QPushButton(QStringLiteral("Redo"));
    redoButton_->setToolTip("Redo the most recently undone pixel-edit or assisted-fill operation.");
    cellInfo_ = new QLabel(QStringLiteral("Hover over the raster to inspect a cell."));
    cellInfo_->setWordWrap(true);
    cellInfo_->setToolTip(
        "Pixel index, raster-local coordinate, sample count, state, and current power.");
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setToolTip("Raster completion and manual-edit summary.");

    auto* fillRow = new QHBoxLayout;
    fillRow->addWidget(fillMissing_);
    fillRow->addWidget(fillSettings);
    fillRow->addWidget(applyFill);
    fillRow->addWidget(showMissing_);
    fillRow->addWidget(blinkMissingButton_);
    fillRow->addWidget(currentMissingOnly_);
    fillRow->addStretch();
    auto* editRow = new QHBoxLayout;
    editRow->addWidget(editPixels_);
    editRow->addWidget(new QLabel(QStringLiteral("Power [dBFS]")));
    editRow->addWidget(editPower_);
    editRow->addWidget(new QLabel(QStringLiteral("Brush")));
    editRow->addWidget(brushSize_);
    editRow->addWidget(suggestButton);
    editRow->addWidget(assistedFillButton);
    editRow->addWidget(unlockMeasured_);
    editRow->addWidget(undoButton_);
    editRow->addWidget(redoButton_);
    editRow->addStretch();
    auto* radioControls = new QGroupBox(QStringLiteral("Radio Map Raster Completion / Editing"));
    auto* radioLayout = new QVBoxLayout(radioControls);
    radioLayout->addLayout(fillRow);
    radioLayout->addLayout(editRow);
    radioLayout->addWidget(cellInfo_);
    radioLayout->addWidget(summary_);
    radioControls->setVisible(imageType_ == 2);

    auto* saveButton = new QPushButton(QStringLiteral("Save PNG and Metadata..."));
    saveButton->setToolTip(
        "Save the current final raster and JSON metadata. The missing overlay is never exported.");
    exportAlternateColorMode_ = new QCheckBox;
    const bool primaryIsColor = bundle_.render_options.color_mode == RasterColorMode::Color;
    exportAlternateColorMode_->setText(primaryIsColor ? QStringLiteral("Also export Gray Radio Map")
                                       : QStringLiteral("Also export RGB Radio Map"));
    exportAlternateColorMode_->setToolTip(
        QStringLiteral("Render both files from the same final Radio Map cell values so completion, "
                       "assisted fill, and manual edits remain pixel-for-pixel consistent."));
    exportAlternateColorMode_->setVisible(imageType_ == 2);
    exportAlternateColorMode_->setEnabled(imageType_ == 2);
    auto* closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setToolTip("Close this Preview session without saving.");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(exportAlternateColorMode_);
    buttons->addStretch();
    buttons->addWidget(saveButton);
    buttons->addWidget(closeButton);
    auto* root = new QVBoxLayout(this);
    root->addWidget(view_, 1);
    root->addWidget(radioControls);
    if (!bundle_.warnings.isEmpty()) {
        auto* warnings = new QLabel(bundle_.warnings.join(QStringLiteral("; ")));
        warnings->setWordWrap(true);
        root->addWidget(warnings);
    }
    root->addLayout(buttons);

    connect(fillMissing_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            applyCompletion();
        } else {
            rebuildRadioState();
        }
    });
    connect(fillSettings, &QPushButton::clicked, this, [this] {
        if (editCompletionSettings(&completionSettings_, this) && fillMissing_->isChecked()) {
            applyCompletion();
        }
    });
    connect(applyFill, &QPushButton::clicked, this, [this] {
        applyCompletion();
    });
    connect(showMissing_, &QCheckBox::toggled, this, [this] {
        updateMissingOverlay();
    });
    connect(blinkMissingButton_, &QPushButton::toggled, this, &RasterPreviewDialog::setBlinkMissing);
    connect(currentMissingOnly_, &QCheckBox::toggled, this, [this] {
        updateMissingOverlay();
    });
    connect(suggestButton, &QPushButton::clicked, this, &RasterPreviewDialog::suggestPower);
    connect(assistedFillButton, &QPushButton::clicked, this, &RasterPreviewDialog::assistedFill);
    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(500);
    connect(blinkTimer_, &QTimer::timeout, this, [this] {
        blinkPhase_ = !blinkPhase_;
        const bool visible = blinkMissingButton_->isChecked() ? blinkPhase_ : showMissing_->isChecked();
        view_->setOverlayVisible(visible);
    });
    connect(undoButton_, &QPushButton::clicked, this, [this] {
        undo();
    });
    connect(redoButton_, &QPushButton::clicked, this, [this] {
        redo();
    });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        save();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    if (imageType_ == 2) {
        initializeRadioSession();
    }
    updatePreview();
}

RasterPreviewDialog::~RasterPreviewDialog() = default;

void RasterPreviewDialog::initializeRadioSession() {
    completion_.power_dbfs = bundle_.radio_grid.power_dbfs;
    completion_.state.assign(bundle_.radio_grid.cellCount(), RasterCellState::Missing);
    originalMissingMask_.assign(bundle_.radio_grid.cellCount(), 0);
    for (std::size_t index = 0; index < bundle_.radio_grid.cellCount(); ++index) {
        if (bundle_.radio_grid.building_mask[index] != 0) {
            completion_.state[index] = RasterCellState::Building;
        } else if (bundle_.radio_grid.sample_count[index] > 0 &&
                   std::isfinite(bundle_.radio_grid.power_dbfs[index])) {
            completion_.state[index] = RasterCellState::MeasuredAggregate;
            ++completion_.measured_cells;
        } else {
            if (bundle_.radio_grid.sample_count[index] == 0) {
                originalMissingMask_[index] = 1;
            }
            ++completion_.missing_cells;
        }
    }
    rebuildRadioState();
}

void RasterPreviewDialog::applyCompletion() {
    if (imageType_ != 2) {
        return;
    }
    if (!fillMissing_->isChecked()) {
        const QSignalBlocker blocker(fillMissing_);
        fillMissing_->setChecked(true);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    completion_ = RasterCompletion::complete(bundle_.radio_grid, completionSettings_);
    QApplication::restoreOverrideCursor();
    rebuildRadioState();
}

void RasterPreviewDialog::rebuildRadioState() {
    if (imageType_ != 2) {
        return;
    }
    if (fillMissing_->isChecked()) {
        displayedPower_ = completion_.power_dbfs;
        displayedState_ = completion_.state;
    } else {
        displayedPower_ = bundle_.radio_grid.power_dbfs;
        displayedState_.assign(bundle_.radio_grid.cellCount(), RasterCellState::Missing);
        for (std::size_t index = 0; index < bundle_.radio_grid.cellCount(); ++index) {
            if (bundle_.radio_grid.building_mask[index] != 0) {
                displayedState_[index] = RasterCellState::Building;
            } else if (bundle_.radio_grid.sample_count[index] > 0 &&
                       std::isfinite(bundle_.radio_grid.power_dbfs[index])) {
                displayedState_[index] = RasterCellState::MeasuredAggregate;
            }
        }
    }
    int manualCount = 0;
    int assistedCount = 0;
    for (const auto& entry : rasterOverrides_) {
        displayedPower_[entry.first] = entry.second.power_dbfs;
        displayedState_[entry.first] = entry.second.state;
        if (entry.second.state == RasterCellState::AssistedProgressive) {
            ++assistedCount;
        } else {
            ++manualCount;
        }
    }
    const int remainingMissing = static_cast<int>(
                                     std::count(displayedState_.begin(), displayedState_.end(), RasterCellState::Missing));
    QString summary =
        QStringLiteral(
            "Original missing: %1 | Measured: %2 | Primary fill: %3 | Nearest fallback: %4 | "
            "Assisted: %5 | Manual: %6 | Remaining missing: %7")
        .arg(static_cast<int>(std::count(originalMissingMask_.begin(), originalMissingMask_.end(),
                                         std::uint8_t{1})))
        .arg(completion_.measured_cells)
        .arg(fillMissing_->isChecked() ? completion_.interpolated_cells : 0)
        .arg(fillMissing_->isChecked() ? completion_.fallback_cells : 0)
        .arg(assistedCount)
        .arg(manualCount)
        .arg(remainingMissing);
    if (fillMissing_->isChecked()) {
        summary += QStringLiteral(" | Rejected: support %1, barrier %2, variance %3")
                   .arg(completion_.rejected_support)
                   .arg(completion_.rejected_barrier)
                   .arg(completion_.rejected_variance);
        if (!completion_.warnings.isEmpty()) {
            summary += QStringLiteral(" | Warning: ") + completion_.warnings.join(QStringLiteral("; "));
        }
    }
    summary_->setText(summary);
    undoButton_->setEnabled(!undoStack_.empty());
    redoButton_->setEnabled(!redoStack_.empty());
    updatePreview();
}

QImage RasterPreviewDialog::radioMapImage(RasterColorMode colorMode) const {
    return RasterRenderer::renderRadioGrid(bundle_.radio_grid, displayedPower_,
                                           bundle_.default_power_dbfs, bundle_.power_min_dbfs,
                                           bundle_.power_max_dbfs, colorMode);
}

QImage RasterPreviewDialog::currentExportImage() const {
    switch (imageType_) {
    case 0:
        return bundle_.building;
    case 1:
        return bundle_.tx;
    case 2:
        return radioMapImage(bundle_.render_options.color_mode);
    default:
        return bundle_.count;
    }
}

QImage RasterPreviewDialog::currentPresentationImage() const {
    const QImage image = currentExportImage();
    // if (!bundle_.render_options.add_colorbar || imageType_ < 2) {
    if (!bundle_.render_options.add_colorbar || imageType_ != 2) {
        return image;
    }
    const bool radioMap = imageType_ == 2;
    return RasterRenderer::addColorbar(
               image, radioMap ? bundle_.power_min_dbfs : 0.0,
               radioMap ? bundle_.power_max_dbfs : bundle_.max_sample_count,
               radioMap ? QStringLiteral("Power [dBFS]") : QStringLiteral("Sample count"),
               bundle_.render_options.color_mode);
}

void RasterPreviewDialog::updatePreview() {
    QImage preview = currentExportImage().convertToFormat(QImage::Format_ARGB32);
    // if (bundle_.render_options.add_colorbar && imageType_ >= 2) {
    if (bundle_.render_options.add_colorbar && imageType_ == 2) {
        const bool radioMap = imageType_ == 2;
        preview = RasterRenderer::addColorbar(
                      preview, radioMap ? bundle_.power_min_dbfs : 0.0,
                      radioMap ? bundle_.power_max_dbfs : bundle_.max_sample_count,
                      radioMap ? QStringLiteral("Power [dBFS]") : QStringLiteral("Sample count"),
                      bundle_.render_options.color_mode);
    }
    view_->setImage(preview);
    updateMissingOverlay();
}

void RasterPreviewDialog::updateMissingOverlay() {
    if (imageType_ != 2 || displayedState_.size() != bundle_.radio_grid.cellCount()) {
        return;
    }
    const int width = bundle_.render_options.add_colorbar ? bundle_.radio_grid.width_px + 150
                      : bundle_.radio_grid.width_px;
    const int height = bundle_.render_options.add_colorbar
                       ? std::max(bundle_.radio_grid.height_px, 140)
                       : bundle_.radio_grid.height_px;
    QImage overlay(width, height, QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setPen(Qt::NoPen);
    int highlightedCells = 0;
    for (int y = 0; y < bundle_.radio_grid.height_px; ++y) {
        for (int x = 0; x < bundle_.radio_grid.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * bundle_.radio_grid.width_px + x;
            const bool currentMissing = displayedState_[index] == RasterCellState::Missing;
            const bool originalMissing = originalMissingMask_[index] != 0;
            if (currentMissing) {
                painter.fillRect(x, y, 1, 1, QColor(255, 0, 190, 210));
                ++highlightedCells;
            } else if (!currentMissingOnly_->isChecked() && originalMissing) {
                painter.fillRect(x, y, 1, 1, QColor(0, 230, 255, 185));
                ++highlightedCells;
            }
        }
    }
    blinkMissingButton_->setEnabled(highlightedCells > 0);
    if (highlightedCells == 0 && blinkMissingButton_->isChecked()) {
        const QSignalBlocker blocker(blinkMissingButton_);
        blinkMissingButton_->setChecked(false);
        blinkTimer_->stop();
        blinkPhase_ = true;
    }
    const bool visible = blinkMissingButton_->isChecked() ? blinkPhase_ : showMissing_->isChecked();
    view_->setOverlay(overlay, visible);
}

void RasterPreviewDialog::setBlinkMissing(bool enabled) {
    blinkPhase_ = true;
    if (enabled) {
        blinkTimer_->start();
    } else {
        blinkTimer_->stop();
    }
    updateMissingOverlay();
}

void RasterPreviewDialog::inspectPixel(int x, int y) {
    if (imageType_ != 2 || x < 0 || y < 0 || x >= bundle_.radio_grid.width_px ||
            y >= bundle_.radio_grid.height_px) {
        return;
    }
    const auto index = static_cast<std::size_t>(y) * bundle_.radio_grid.width_px + x;
    lastRasterIndex_ = index;
    const double localX =
        bundle_.frame.min_east_m + (static_cast<double>(x) + 0.5) * bundle_.frame.meters_per_pixel;
    const double localY =
        bundle_.frame.max_north_m - (static_cast<double>(y) + 0.5) * bundle_.frame.meters_per_pixel;
    const QString power = std::isfinite(displayedPower_[index])
                          ? QString::number(displayedPower_[index], 'f', 3)
                          : QStringLiteral("n.a.");
    cellInfo_->setText(
        QStringLiteral("Pixel (%1, %2) | Raster local (%3, %4) m | Samples %5 | %6 | Power %7 dBFS")
        .arg(x)
        .arg(y)
        .arg(localX, 0, 'f', 3)
        .arg(localY, 0, 'f', 3)
        .arg(bundle_.radio_grid.sample_count[index])
        .arg(rasterCellStateName(displayedState_[index]))
        .arg(power));
}

void RasterPreviewDialog::editPixel(int x, int y, Qt::MouseButton button) {
    if (imageType_ != 2 || x < 0 || y < 0 || x >= bundle_.radio_grid.width_px ||
            y >= bundle_.radio_grid.height_px || !editPixels_->isChecked() ||
            (button != Qt::LeftButton && button != Qt::RightButton)) {
        return;
    }
    const int brush = brushSize_->currentData().toInt();
    const int radius = brush / 2;
    std::vector<ManualChange> command;
    for (int row = std::max(0, y - radius);
            row <= std::min(bundle_.radio_grid.height_px - 1, y + radius); ++row) {
        for (int column = std::max(0, x - radius);
                column <= std::min(bundle_.radio_grid.width_px - 1, x + radius); ++column) {
            const auto index = static_cast<std::size_t>(row) * bundle_.radio_grid.width_px + column;
            if (bundle_.radio_grid.building_mask[index] != 0) {
                continue;
            }
            const bool measured = bundle_.radio_grid.sample_count[index] > 0;
            if (measured && !unlockMeasured_->isChecked()) {
                continue;
            }
            const auto existing = rasterOverrides_.find(index);
            ManualChange change;
            change.index = index;
            change.before_present = existing != rasterOverrides_.end();
            if (change.before_present) {
                change.before_value = existing->second;
            }
            change.after_present = button == Qt::LeftButton;
            change.after_value = {editPower_->value(), RasterCellState::ManualRasterValue, 1.0};
            if (change.before_present == change.after_present &&
                    (!change.after_present ||
                     (change.before_value.power_dbfs == change.after_value.power_dbfs &&
                      change.before_value.state == change.after_value.state))) {
                continue;
            }
            command.push_back(change);
        }
    }
    if (command.empty()) {
        return;
    }
    applyHistoryCommand(command, true);
    undoStack_.push_back(command);
    redoStack_.clear();
    rebuildRadioState();
}

void RasterPreviewDialog::applyHistoryCommand(const std::vector<ManualChange>& command,
        bool forward) {
    for (const auto& change : command) {
        const bool present = forward ? change.after_present : change.before_present;
        const RasterOverride value = forward ? change.after_value : change.before_value;
        if (present) {
            rasterOverrides_[change.index] = value;
        } else {
            rasterOverrides_.erase(change.index);
        }
    }
}

void RasterPreviewDialog::suggestPower() {
    if (imageType_ != 2 || lastRasterIndex_ >= bundle_.radio_grid.cellCount() ||
            bundle_.radio_grid.building_mask[lastRasterIndex_] != 0) {
        QMessageBox::information(this, QStringLiteral("Suggest Power"),
                                 QStringLiteral("Hover over a non-building raster pixel first."));
        return;
    }
    const auto suggestion = RasterInpainting::suggest(
                                bundle_.radio_grid, displayedPower_, displayedState_, lastRasterIndex_, inpaintingSettings_);
    if (!suggestion.valid) {
        QMessageBox::information(
            this, QStringLiteral("No Reliable Suggestion"),
            QStringLiteral("The inspected pixel has insufficient local non-building support."));
        return;
    }
    editPower_->setValue(suggestion.power_dbfs);
    cellInfo_->setText(
        QStringLiteral("Suggested %1 dBFS | Confidence %2 | Sources %3 | Model: %4. The raster "
                       "was not changed.")
        .arg(suggestion.power_dbfs, 0, 'f', 3)
        .arg(suggestion.confidence, 0, 'f', 3)
        .arg(suggestion.source_count)
        .arg(suggestion.model));
}

void RasterPreviewDialog::assistedFill() {
    if (imageType_ != 2) {
        return;
    }
    const bool hasSelectedMissing = lastRasterIndex_ < displayedState_.size() &&
                                    displayedState_[lastRasterIndex_] == RasterCellState::Missing &&
                                    bundle_.radio_grid.building_mask[lastRasterIndex_] == 0;
    if (!editInpaintingSettings(&inpaintingSettings_, hasSelectedMissing, this)) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = RasterInpainting::fill(bundle_.radio_grid, displayedPower_, displayedState_,
                        lastRasterIndex_, inpaintingSettings_);
    QApplication::restoreOverrideCursor();
    if (result.cells.empty()) {
        QMessageBox::information(this, QStringLiteral("Assisted Fill"),
                                 result.warning.isEmpty()
                                 ? QStringLiteral("No cell met the configured fill criteria.")
                                 : result.warning);
        return;
    }
    std::vector<double> previewPower = displayedPower_;
    for (const auto& cell : result.cells) {
        previewPower[cell.index] = cell.power_dbfs;
    }
    const QImage assistedPreview = RasterRenderer::renderRadioGrid(
                                       bundle_.radio_grid, previewPower, bundle_.default_power_dbfs, bundle_.power_min_dbfs,
                                       bundle_.power_max_dbfs, bundle_.render_options.color_mode);
    QDialog confirmation(this);
    confirmation.setWindowTitle(QStringLiteral("Assisted Fill Preview"));
    auto* previewLabel = new QLabel;
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setPixmap(QPixmap::fromImage(assistedPreview)
                            .scaled(720, 620, Qt::KeepAspectRatio, Qt::FastTransformation));
    previewLabel->setToolTip(
        "Preview of the proposed assisted values. Building and existing raster cells are "
        "unchanged.");
    auto* previewSummary = new QLabel(
        QStringLiteral("Proposed: %1 / %2 cells | Layers: %3 | Mean confidence: %4 | Minimum "
                       "confidence: %5 | Remaining: %6")
        .arg(qulonglong(result.cells.size()))
        .arg(result.selected_missing_cells)
        .arg(result.completed_layers)
        .arg(result.mean_applied_confidence, 0, 'f', 3)
        .arg(result.minimum_applied_confidence, 0, 'f', 3)
        .arg(result.remaining_missing_cells));
    previewSummary->setWordWrap(true);
    auto* previewButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    previewButtons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply"));
    previewButtons->button(QDialogButtonBox::Ok)
    ->setToolTip("Apply every proposed value as one undoable operation.");
    previewButtons->button(QDialogButtonBox::Cancel)
    ->setToolTip("Discard the proposal without changing the raster.");
    connect(previewButtons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
    connect(previewButtons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
    auto* previewLayout = new QVBoxLayout(&confirmation);
    previewLayout->addWidget(previewLabel, 1);
    previewLayout->addWidget(previewSummary);
    previewLayout->addWidget(previewButtons);
    confirmation.resize(820, 740);
    if (confirmation.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<ManualChange> command;
    command.reserve(result.cells.size());
    for (const auto& cell : result.cells) {
        ManualChange change;
        change.index = cell.index;
        const auto existing = rasterOverrides_.find(cell.index);
        change.before_present = existing != rasterOverrides_.end();
        if (change.before_present) {
            change.before_value = existing->second;
        }
        change.after_present = true;
        change.after_value = {cell.power_dbfs, RasterCellState::AssistedProgressive, cell.confidence};
        command.push_back(change);
    }
    applyHistoryCommand(command, true);
    undoStack_.push_back(command);
    redoStack_.clear();
    lastInpaintingResult_ = result;
    ++assistedApplyCount_;
    rebuildRadioState();
    QString message = QStringLiteral(
                          "Filled %1 of %2 selected missing cells in %3 synchronized layers. "
                          "Mean confidence: %4; minimum confidence: %5.")
                      .arg(qulonglong(result.cells.size()))
                      .arg(result.selected_missing_cells)
                      .arg(result.completed_layers)
                      .arg(result.mean_applied_confidence, 0, 'f', 3)
                      .arg(result.minimum_applied_confidence, 0, 'f', 3);
    if (!result.warning.isEmpty()) {
        message += QStringLiteral("\n") + result.warning;
    }
    QMessageBox::information(this, QStringLiteral("Assisted Fill Applied"), message);
}

void RasterPreviewDialog::undo() {
    if (undoStack_.empty()) {
        return;
    }
    const auto command = undoStack_.back();
    undoStack_.pop_back();
    applyHistoryCommand(command, false);
    redoStack_.push_back(command);
    rebuildRadioState();
}

void RasterPreviewDialog::redo() {
    if (redoStack_.empty()) {
        return;
    }
    const auto command = redoStack_.back();
    redoStack_.pop_back();
    applyHistoryCommand(command, true);
    undoStack_.push_back(command);
    rebuildRadioState();
}

QJsonObject RasterPreviewDialog::currentMetadata() const {
    QJsonObject metadata = metadata_;
    metadata["image_type"] = imageKind_;
    if (imageType_ == 2) {
        const int originalMissing = static_cast<int>(
                                        std::count(originalMissingMask_.begin(), originalMissingMask_.end(), std::uint8_t{1}));
        int currentMissing = 0;
        for (std::size_t index = 0; index < originalMissingMask_.size(); ++index) {
            if (originalMissingMask_[index] != 0 && displayedState_[index] == RasterCellState::Missing) {
                ++currentMissing;
            }
        }
        metadata["raster_completion_enabled"] = fillMissing_->isChecked();
        metadata["raster_completion_settings"] = completionSettings_.toJson();
        metadata["raster_interpolated_cells"] =
            fillMissing_->isChecked() ? completion_.interpolated_cells : 0;
        metadata["nearest_fallback_cells"] = fillMissing_->isChecked() ? completion_.fallback_cells : 0;
        metadata["original_missing_free_space_cells"] = originalMissing;
        metadata["final_missing_free_space_cells"] = currentMissing;
        metadata["resolved_original_missing_cells"] = originalMissing - currentMissing;
        metadata["assisted_fill_sessions_confirmed"] = assistedApplyCount_;
        metadata["assisted_fill_settings"] = inpaintingSettings_.toJson();
        metadata["last_assisted_selected_cells"] = lastInpaintingResult_.selected_missing_cells;
        metadata["last_assisted_filled_cells"] = static_cast<int>(lastInpaintingResult_.cells.size());
        metadata["last_assisted_remaining_cells"] = lastInpaintingResult_.remaining_missing_cells;
        metadata["last_assisted_completed_layers"] = lastInpaintingResult_.completed_layers;
        metadata["last_assisted_mean_confidence"] = lastInpaintingResult_.mean_applied_confidence;
        metadata["sample_count_modified_by_completion"] = false;
        QJsonArray edits;
        QJsonArray manualEdits;
        QJsonArray assistedEdits;
        int manualCount = 0;
        int assistedCount = 0;
        for (const auto& entry : rasterOverrides_) {
            const int y = static_cast<int>(entry.first / bundle_.radio_grid.width_px);
            const int x = static_cast<int>(entry.first % bundle_.radio_grid.width_px);
            const bool assisted = entry.second.state == RasterCellState::AssistedProgressive;
            if (assisted) {
                ++assistedCount;
            } else {
                ++manualCount;
            }
            const QJsonObject edit{
                {"pixel_x", x},
                {"pixel_y", y},
                {"power_dbfs", entry.second.power_dbfs},
                {"source", assisted ? QStringLiteral("assisted_progressive") : QStringLiteral("manual")},
                {"confidence", entry.second.confidence}};
            edits.append(edit);
            if (assisted) {
                assistedEdits.append(edit);
            } else {
                manualEdits.append(edit);
            }
        }
        metadata["manual_raster_cells"] = manualCount;
        metadata["assisted_progressive_cells"] = assistedCount;
        metadata["manual_raster_overrides"] = manualEdits;
        metadata["assisted_raster_overrides"] = assistedEdits;
        metadata["raster_overrides"] = edits;
    }
    return metadata;
}

void RasterPreviewDialog::save() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Raster"), QString(),
                   QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    path = pngPath(path);
    const QImage image = currentExportImage();
    if (!image.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Save Failed"),
                             QStringLiteral("Could not save PNG."));
        return;
    }
    const bool addColorbar = bundle_.render_options.add_colorbar && imageType_ == 2;
    QString colorbarPath;
    if (addColorbar) {
        colorbarPath = path.left(path.size() - 4) + QStringLiteral("_with_colorbar.png");
        if (!currentPresentationImage().save(colorbarPath)) {
            QMessageBox::warning(
                this, QStringLiteral("Save Failed"),
                QStringLiteral("The aligned PNG was saved, but the colorbar companion failed."));
            return;
        }
    }
    const bool exportAlternate = imageType_ == 2 && exportAlternateColorMode_->isChecked();
    QString alternatePath;
    QString alternateColorbarPath;
    RasterColorMode alternateMode = RasterColorMode::Grayscale;
    if (exportAlternate) {
        alternateMode = bundle_.render_options.color_mode == RasterColorMode::Grayscale
                        ? RasterColorMode::Color
                        : RasterColorMode::Grayscale;
        const QFileInfo primaryInfo(path);
        alternatePath = primaryInfo.dir().filePath(
                            RasterRenderer::alternateRadioMapFileName(primaryInfo.fileName(), alternateMode));
        const QImage alternateImage = radioMapImage(alternateMode);
        if (!alternateImage.save(alternatePath)) {
            QMessageBox::warning(
                this, QStringLiteral("Save Failed"),
                QStringLiteral("The primary Radio Map was saved, but its alternate color-mode "
                               "companion could not be saved."));
            return;
        }
        if (addColorbar) {
            alternateColorbarPath =
                alternatePath.left(alternatePath.size() - 4) + QStringLiteral("_with_colorbar.png");
            const QImage alternatePresentation = RasterRenderer::addColorbar(
                    alternateImage, bundle_.power_min_dbfs, bundle_.power_max_dbfs,
                    QStringLiteral("Power [dBFS]"), alternateMode);
            if (!alternatePresentation.save(alternateColorbarPath)) {
                QMessageBox::warning(
                    this, QStringLiteral("Save Failed"),
                    QStringLiteral("Both aligned Radio Maps were saved, but the alternate colorbar "
                                   "companion failed."));
                return;
            }
        }
    }
    QJsonObject metadata = currentMetadata();
    metadata["colorbar_companion"] = addColorbar ? QFileInfo(colorbarPath).fileName() : QString();
    QString metadataError;
    if (!RasterRenderer::saveMetadata(path.left(path.size() - 4) + QStringLiteral(".json"), metadata,
                                      &metadataError)) {
        QMessageBox::warning(this, QStringLiteral("Metadata Failed"), metadataError);
        return;
    }
    if (exportAlternate) {
        QJsonObject alternateMetadata = currentMetadata();
        alternateMetadata["color_mode"] = alternateMode == RasterColorMode::Color
                                          ? QStringLiteral("color")
                                          : QStringLiteral("grayscale");
        alternateMetadata["colorbar_companion"] =
            !alternateColorbarPath.isEmpty() ? QFileInfo(alternateColorbarPath).fileName() : QString();
        const QString alternateMetadataPath =
            alternatePath.left(alternatePath.size() - 4) + QStringLiteral(".json");
        if (!RasterRenderer::saveMetadata(alternateMetadataPath, alternateMetadata, &metadataError)) {
            QMessageBox::warning(
                this, QStringLiteral("Metadata Failed"),
                QStringLiteral("The Radio Map images were saved, but the alternate color-mode metadata "
                               "could not be saved: %1")
                .arg(metadataError));
            return;
        }
    }
    accept();
}

}  // namespace b210post
