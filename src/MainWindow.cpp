#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "BuildingShapeDialog.hpp"
#include "CoordinateTransform.hpp"
#include "LocalFillDialog.hpp"
#include "RasterPreviewDialog.hpp"
#include "RasterRenderer.hpp"
#include "RawRecoveryDialog.hpp"
#include "SettingsDialog.hpp"
#include "SpatialWidget.hpp"

#if defined(AUTO_VERSION) && AUTO_VERSION
#include "version.h"
#else
#pragma message("AUTO_VERSION is disabled, using default version 0.9.0")
#undef APP_VERSION
#define APP_VERSION "0.9.0"
#endif

namespace b210post {
namespace {

QAction* action(QToolBar* t, const QString& name, const QString& tip) {
  auto* a = t->addAction(name);
  a->setToolTip(tip);
  a->setStatusTip(tip);
  return a;
}

QAction* menuAction(QObject* parent, const QString& name, const QString& tip) {
  auto* action = new QAction(name, parent);
  action->setToolTip(tip);
  action->setStatusTip(tip);
  return action;
}

EnuReference fallback(const AppSettings& s) {
  return {s.fallback_latitude_deg, s.fallback_longitude_deg, s.fallback_altitude_m, true,
          ReferenceSource::SettingsFallback};
}

bool finiteGeo(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 &&
         lon <= 180;
}

QDoubleSpinBox* regionSpin(double min, double max, int decimals, const QString& tip) {
  auto* s = new QDoubleSpinBox;
  s->setRange(min, max);
  s->setDecimals(decimals);
  s->setToolTip(tip);
  s->setKeyboardTracking(false);
  return s;
}

class ImportDialog : public QDialog {
 public:
  QLineEdit *a = new QLineEdit, *b = new QLineEdit;

  explicit ImportDialog(QWidget* p) : QDialog(p) {
    setWindowTitle("Import Data");
    setMinimumWidth(720);
    a->setReadOnly(true);
    b->setReadOnly(true);
    a->setToolTip(
        "Required strict >=V0.9 Type-A or B210RadioMapPost Private Type-A CSV. The file is closed "
        "after parsing and never overwritten.");
    b->setToolTip(
        "Optional strict Type-B, Private Type-B, Raster Frame, or Private Raster Frame CSV.");
    auto* fa = new QPushButton("Browse Type-A...");
    auto* fb = new QPushButton("Browse Type-B / Frame...");
    fa->setToolTip("Choose the required power/GNSS Type-A CSV.");
    fb->setToolTip("Choose an optional standard Type-B or Raster Frame CSV.");
    connect(fa, &QPushButton::clicked, this, [this] {
      const auto p = QFileDialog::getOpenFileName(this, "Type-A CSV", {}, "CSV (*.csv)");
      if (!p.isEmpty())
        a->setText(p);
    });
    connect(fb, &QPushButton::clicked, this, [this] {
      const auto p =
          QFileDialog::getOpenFileName(this, "Type-B or Raster Frame CSV", {}, "CSV (*.csv)");
      if (!p.isEmpty())
        b->setText(p);
    });
    auto* form = new QFormLayout;
    auto* ar = new QHBoxLayout;
    ar->addWidget(a, 1);
    ar->addWidget(fa);
    auto* br = new QHBoxLayout;
    br->addWidget(b, 1);
    br->addWidget(fb);
    form->addRow("Type-A (required)", ar);
    form->addRow("Type-B / Frame (optional)", br);
    auto* ok = new QPushButton("Import");
    auto* cancel = new QPushButton("Cancel");
    ok->setToolTip("Import Type-A and optionally replace the held building frame.");
    cancel->setToolTip("Close without importing files.");
    connect(ok, &QPushButton::clicked, this, [this] {
      if (a->text().isEmpty())
        QMessageBox::warning(this, "Type-A Required", "Choose a Type-A CSV first.");
      else
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(buttons);
  }
};

class ZoomGraphicsView : public QGraphicsView {
 public:
  using QGraphicsView::QGraphicsView;

 protected:
  void wheelEvent(QWheelEvent* e) override {
    const qreal factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
    e->accept();
  }
};

}  // namespace

MainWindow::MainWindow() {
  setWindowTitle(QStringLiteral("B210 Radio Map Post Processor V%1").arg(APP_VERSION));
  resize(1500, 930);
  loadSettings();
  interpolationMethods_ = createBuiltInInterpolationMethods();
  frameReference_ = fallback(settings_);
  auto* toolbar = new QToolBar("Main tools");
  toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar->setMovable(false);
  addToolBar(toolbar);
  auto* importAction =
      action(toolbar, "Import",
             "Load strict standard or post-only private CSV data into memory. Imported sources "
             "are closed and protected from overwrite.");
  auto* saveAAction =
      action(toolbar, "Save Type-A",
             "Save standard Type-A to a new/output path. An imported source can never be "
             "overwritten.");
  auto* saveBAction =
      action(toolbar, "Save Type-B",
             "Save a strict B210RadioMap-compatible Type-B CSV without Raster Frame records.");
  auto* rawAction =
      menuAction(this, "Raw Recovery", "Open the standalone raw_timeline.csv recovery window.");
  auto* settingsAction =
      action(toolbar, "Settings",
             "Configure the fallback geographic anchor and canvas display. Standard Type-A ENU "
             "plotting always uses its first valid RX point, not this anchor.");
  auto* privateAction = menuAction(
      this, "Create Private Copy...",
      "Export post-processor-only CSV copies with WGS84 and geographic origin removed. ENU "
      "geometry, scale, and orientation are preserved exactly.");
  exportSyntheticAction_ =
      menuAction(this, "Export Synthetic WGS84 Copies...",
                 "Use the Settings geographic anchor to generate standard Type-A, Type-B, and "
                 "available Raster Frame copies without modifying the loaded Private ENU data.");
  toolbar->addSeparator();
  mode_ = new QComboBox;
  mode_->addItem("Edit Power / GNSS", int(SpatialWidget::EditLayer::Radio));
  mode_->addItem("Edit Building GNSS", int(SpatialWidget::EditLayer::Building));
  mode_->setToolTip(
      "Choose the active editable layer. The other layer remains visible with reduced opacity.");
  toolbar->addWidget(mode_);
  placeButton_ = new QPushButton("Place Power Point");
  placeButton_->setCheckable(true);
  placeButton_->setToolTip("Toggle manual point placement on the project ENU map.");
  toolbar->addWidget(placeButton_);
  auto* deletePoint = new QPushButton("Delete Point");
  deletePoint->setToolTip("Delete all selected Type-A or Type-B points after one confirmation.");
  toolbar->addWidget(deletePoint);
  toggleRoleButton_ = new QPushButton("Toggle RX / TX");
  toggleRoleButton_->setToolTip(
      "Switch every selected Type-A point between RX and TX without changing other fields.");
  toolbar->addWidget(toggleRoleButton_);
  localFillButton_ = new QPushButton("Local Fill...");
  localFillButton_->setToolTip(
      "Open the unique non-modal local interpolation window. Available only in Power / GNSS "
      "mode; generated RX points are previewed before being appended.");
  toolbar->addWidget(localFillButton_);
  buildingShapeButton_ = new QPushButton("Building Shapes...");
  buildingShapeButton_->setToolTip(
      "Place parameterized building outlines and temporary measurement rulers on the ENU map.");
  toolbar->addWidget(buildingShapeButton_);
  connectPointsButton_ = new QPushButton("Connect Building Points");
  connectPointsButton_->setToolTip(
      "Connect exactly two selected Type-B points. Ctrl-click points on the map to select both.");
  toolbar->addWidget(connectPointsButton_);
  deleteEdgeButton_ = new QPushButton("Delete Edge");
  deleteEdgeButton_->setToolTip("Delete the building edge highlighted on the map.");
  toolbar->addWidget(deleteEdgeButton_);
  renderType_ = new QComboBox;
  renderType_->addItems(
      {"Building Mask", "TX Mask", "Radio Map", /*"Sample Count"*/ "Sample Site Mask"});
  renderType_->setToolTip("Choose the fixed-frame raster to preview and save.");
  auto* renderButton = new QPushButton("Render...");
  renderButton->setToolTip("Render with the exact region and resolution configured below.");
  auto* checkButton = new QPushButton("Render Check");
  checkButton->setToolTip("Overlay rendered images for visual alignment checking.");

  auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  fileMenu->addAction(importAction);
  auto* saveMenu = fileMenu->addMenu(QStringLiteral("&Save"));
  saveMenu->addAction(saveAAction);
  saveMenu->addAction(saveBAction);
  saveMenu->addSeparator();
  saveMenu->addAction(exportSyntheticAction_);
  fileMenu->addSeparator();
  fileMenu->addAction(privateAction);
  auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
  auto* placeMenuAction = menuAction(this, "Place Point",
                                     "Toggle point placement for the currently active edit layer.");
  auto* deleteMenuAction = menuAction(this, "Delete Selected Points",
                                      "Delete selected points in the active edit layer.");
  auto* toggleMenuAction =
      menuAction(this, "Toggle RX / TX", "Toggle selected Type-A points between RX and TX.");
  auto* localFillMenuAction = menuAction(
      this, "Local Fill...", "Open point-level local interpolation in Power / GNSS mode.");
  auto* connectMenuAction =
      menuAction(this, "Connect Building Points", "Connect exactly two selected Type-B points.");
  auto* deleteEdgeMenuAction =
      menuAction(this, "Delete Building Edge", "Delete the selected Type-B edge.");
  editMenu->addAction(placeMenuAction);
  editMenu->addAction(deleteMenuAction);
  editMenu->addAction(toggleMenuAction);
  editMenu->addAction(localFillMenuAction);
  editMenu->addSeparator();
  editMenu->addAction(connectMenuAction);
  editMenu->addAction(deleteEdgeMenuAction);
  auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
  toolsMenu->addAction(rawAction);
  toolsMenu->addAction(settingsAction);
  auto* renderMenu = menuBar()->addMenu(QStringLiteral("&Render"));
  auto* renderMenuAction =
      menuAction(this, "Render Preview", "Render using the Raster / Export Region settings.");
  renderMenuAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  auto* renderCheckMenuAction = menuAction(this, "Render Alignment Check",
                                           "Overlay rendered images to inspect their alignment.");
  renderCheckMenuAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
  renderMenu->addAction(renderMenuAction);
  renderMenu->addAction(renderCheckMenuAction);

  auto* central = new QWidget;
  setCentralWidget(central);
  map_ = new SpatialWidget;
  map_->setSettings(settings_);
  radioTable_ = new QTableView;
  buildingTable_ = new QTableView;
  radioTable_->setModel(&radioModel_);
  buildingTable_->setModel(&buildingModel_);
  for (auto* table : {radioTable_, buildingTable_}) {
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setStretchLastSection(true);
  }
  radioTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  buildingTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  radioTable_->setToolTip(
      "Unified Type-A RX/TX points. Ctrl toggles rows and Shift selects a continuous range.");
  buildingTable_->setToolTip(
      "Type-B vertices. Ctrl toggles rows and Shift selects a continuous range.");
  auto* rightPane = new QSplitter(Qt::Vertical);
  auto* radioPane = new QWidget;
  auto* radioLayout = new QVBoxLayout(radioPane);
  radioLayout->setContentsMargins(0, 0, 0, 0);
  radioLayout->addWidget(new QLabel("Type-A: RX / TX Power-GNSS points"));
  radioLayout->addWidget(radioTable_);
  auto* buildingPane = new QWidget;
  auto* buildingLayout = new QVBoxLayout(buildingPane);
  buildingLayout->setContentsMargins(0, 0, 0, 0);
  buildingLayout->addWidget(new QLabel("Type-B: Building GNSS points"));
  buildingLayout->addWidget(buildingTable_);
  rightPane->addWidget(radioPane);
  rightPane->addWidget(buildingPane);
  auto* splitter = new QSplitter(Qt::Horizontal);
  splitter->addWidget(map_);
  splitter->addWidget(rightPane);
  splitter->setStretchFactor(0, 58);
  splitter->setStretchFactor(1, 42);

  auto* rasterGroup = new QGroupBox("Raster / Export Region");
  auto* grid = new QGridLayout(rasterGroup);
  frameButton_ = new QPushButton("LOAD FRAME");
  frameButton_->setToolTip(
      "Load a standard Type-B or Raster Frame CSV. When HOLD is active, click to release the held "
      "frame.");
  saveFrameButton_ = new QPushButton("Save Raster Frame...");
  saveFrameButton_->setToolTip(
      "Save a self-contained post-processor Frame CSV. This never overwrites the Type-B "
      "compatibility path.");
  showRegionButton_ = new QPushButton("Show Region");
  showRegionButton_->setCheckable(true);
  showRegionButton_->setToolTip(
      "Show the origin, rotated crosshair, export-frame outline, and outside mask on the ENU "
      "map.");
  left_ = regionSpin(0, 100000, 3, "Distance from project origin to the left raster boundary.");
  right_ =
      regionSpin(.001, 100000, 3, "Distance from project origin to the right raster boundary.");
  bottom_ = regionSpin(0, 100000, 3, "Distance from project origin to the bottom raster boundary.");
  top_ = regionSpin(.001, 100000, 3, "Distance from project origin to the top raster boundary.");
  mpp_ = regionSpin(.001, 1000, 4, "Square-pixel spatial resolution shared by all raster outputs.");
  rotation_ = regionSpin(
      -180.0, 180.0, 3,
      "Counter-clockwise rotation of the export frame around the ENU origin. Exported pixels are "
      "rectified into this rotated local frame.");
  autoPower_ = new QCheckBox("Auto default");
  autoPower_->setToolTip(
      "Use minimum finite valid RX power as the building, empty-pixel, and manual-point default.");
  defaultPower_ = regionSpin(
      -300, 100, 3,
      "Manual building/unmeasured power and initial power of manually placed RX points.");
  powerMin_ = regionSpin(-300, 100, 2, "Power mapped to grayscale 0.");
  powerMax_ = regionSpin(-300, 100, 2, "Power mapped to grayscale 255.");
  colorMode_ = new QComboBox;
  colorMode_->addItem("Grayscale", int(RasterColorMode::Grayscale));
  colorMode_->addItem("Color", int(RasterColorMode::Color));
  colorMode_->setToolTip(
      "Choose grayscale or the shared blue-to-red scientific color palette for exported rasters.");
  colorbar_ = new QCheckBox("Add colorbar companion");
  // colorbar_->setToolTip(
  //     "For Radio Map and Sample Count, save an additional presentation PNG with a colorbar. The "
  //     "main raster keeps its exact aligned dimensions.");
  colorbar_->setToolTip(
      "Save an additional Radio Map presentation PNG with a colorbar. "
      "Binary mask outputs never receive a colorbar.");
  smoothExport_ = new QCheckBox("Smooth export");
  smoothExport_->setToolTip(
      "Antialias building boundaries and TX geometry by supersampling, then return to the exact "
      "configured pixel size. Power and sample-count cells are never blurred or filled.");
  supersampling_ = new QComboBox;
  supersampling_->addItem("2x", 2);
  supersampling_->addItem("4x", 4);
  supersampling_->addItem("8x", 8);
  supersampling_->setToolTip("Temporary supersampling factor. Memory use grows with its square.");
  resamplingFilter_ = new QComboBox;
  resamplingFilter_->addItem("Bilinear", int(RasterResamplingFilter::Bilinear));
  resamplingFilter_->addItem("Bicubic", int(RasterResamplingFilter::Bicubic));
  resamplingFilter_->setToolTip("Filter used when reducing the supersampled image.");
  grid->addWidget(frameButton_, 0, 0);
  grid->addWidget(saveFrameButton_, 0, 1);
  grid->addWidget(showRegionButton_, 0, 2);
  grid->addWidget(new QLabel("Left [m]"), 1, 0);
  grid->addWidget(left_, 1, 1);
  grid->addWidget(new QLabel("Right [m]"), 1, 2);
  grid->addWidget(right_, 1, 3);
  grid->addWidget(new QLabel("Bottom [m]"), 1, 4);
  grid->addWidget(bottom_, 1, 5);
  grid->addWidget(new QLabel("Top [m]"), 1, 6);
  grid->addWidget(top_, 1, 7);
  grid->addWidget(new QLabel("Resolution [m/px]"), 2, 0);
  grid->addWidget(mpp_, 2, 1);
  grid->addWidget(new QLabel("Rotation [deg CCW]"), 2, 2);
  grid->addWidget(rotation_, 2, 3);
  grid->addWidget(autoPower_, 2, 4);
  grid->addWidget(defaultPower_, 2, 5);
  grid->addWidget(new QLabel("Power min / max"), 2, 6);
  auto* powerRange = new QHBoxLayout;
  powerRange->addWidget(powerMin_);
  powerRange->addWidget(powerMax_);
  grid->addLayout(powerRange, 2, 7);
  grid->addWidget(new QLabel("Image mode"), 3, 0);
  grid->addWidget(colorMode_, 3, 1);
  grid->addWidget(colorbar_, 3, 2, 1, 2);
  grid->addWidget(smoothExport_, 3, 4);
  grid->addWidget(supersampling_, 3, 5);
  grid->addWidget(resamplingFilter_, 3, 6, 1, 2);
  grid->addWidget(new QLabel("Output"), 4, 0);
  grid->addWidget(renderType_, 4, 1);
  grid->addWidget(renderButton, 4, 2, 1, 2);
  grid->addWidget(checkButton, 4, 4, 1, 2);
  frameInfo_ = new QLabel;
  frameInfo_->setWordWrap(true);
  frameInfo_->setToolTip(
      "Held-frame source, physical size, pixel dimensions, project origin, and validation state.");
  grid->addWidget(frameInfo_, 5, 0, 1, 8);
  left_->setValue(settings_.raster_left_m);
  right_->setValue(settings_.raster_right_m);
  bottom_->setValue(settings_.raster_bottom_m);
  top_->setValue(settings_.raster_top_m);
  mpp_->setValue(settings_.meters_per_pixel);
  rotation_->setValue(settings_.raster_rotation_deg);
  autoPower_->setChecked(settings_.automatic_default_power);
  defaultPower_->setValue(settings_.manual_default_power_dbfs);
  powerMin_->setValue(settings_.power_min_dbfs);
  powerMax_->setValue(settings_.power_max_dbfs);
  int optionIndex = colorMode_->findData(int(settings_.render_options.color_mode));
  colorMode_->setCurrentIndex(optionIndex >= 0 ? optionIndex : 0);
  colorbar_->setChecked(settings_.render_options.add_colorbar);
  smoothExport_->setChecked(settings_.render_options.smooth_export);
  optionIndex = supersampling_->findData(settings_.render_options.supersampling_factor);
  supersampling_->setCurrentIndex(optionIndex >= 0 ? optionIndex : 1);
  optionIndex = resamplingFilter_->findData(int(settings_.render_options.resampling_filter));
  resamplingFilter_->setCurrentIndex(optionIndex >= 0 ? optionIndex : 1);
  status_ = new QLabel("Import a Type-A CSV to begin.");
  status_->setWordWrap(true);
  status_->setToolTip("Project reference, RX/TX counts, building counts, and unsaved state.");
  auto* root = new QVBoxLayout(central);
  root->addWidget(splitter, 1);
  root->addWidget(rasterGroup);
  root->addWidget(status_);

  radioModel_.setDataset(&radio_);
  buildingModel_.setDataSource(&building_, &radio_.reference);
  connect(importAction, &QAction::triggered, this, &MainWindow::importData);
  connect(saveAAction, &QAction::triggered, this, &MainWindow::saveA);
  connect(saveBAction, &QAction::triggered, this, &MainWindow::saveB);
  connect(rawAction, &QAction::triggered, this, &MainWindow::rawRecovery);
  connect(settingsAction, &QAction::triggered, this, &MainWindow::editSettings);
  connect(privateAction, &QAction::triggered, this, &MainWindow::createPrivateCopy);
  connect(exportSyntheticAction_, &QAction::triggered, this,
          &MainWindow::exportSyntheticWgs84Copies);
  connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setMode);
  connect(placeButton_, &QPushButton::clicked, this, &MainWindow::togglePlace);
  connect(deletePoint, &QPushButton::clicked, this, &MainWindow::deleteSelected);
  connect(deleteEdgeButton_, &QPushButton::clicked, this, &MainWindow::deleteEdge);
  connect(toggleRoleButton_, &QPushButton::clicked, this, &MainWindow::toggleRadioRole);
  connect(localFillButton_, &QPushButton::clicked, this, &MainWindow::openLocalFill);
  connect(buildingShapeButton_, &QPushButton::clicked, this, &MainWindow::openBuildingShapeEditor);
  connect(connectPointsButton_, &QPushButton::clicked, this, &MainWindow::addEdge);
  connect(renderButton, &QPushButton::clicked, this, &MainWindow::render);
  connect(checkButton, &QPushButton::clicked, this, &MainWindow::renderCheck);
  connect(placeMenuAction, &QAction::triggered, placeButton_, &QPushButton::click);
  connect(deleteMenuAction, &QAction::triggered, deletePoint, &QPushButton::click);
  connect(toggleMenuAction, &QAction::triggered, this, &MainWindow::toggleRadioRole);
  connect(localFillMenuAction, &QAction::triggered, this, &MainWindow::openLocalFill);
  connect(connectMenuAction, &QAction::triggered, this, &MainWindow::addEdge);
  connect(deleteEdgeMenuAction, &QAction::triggered, this, &MainWindow::deleteEdge);
  connect(renderMenuAction, &QAction::triggered, this, &MainWindow::render);
  connect(renderCheckMenuAction, &QAction::triggered, this, &MainWindow::renderCheck);
  connect(frameButton_, &QPushButton::clicked, this, &MainWindow::loadOrReleaseFrame);
  connect(saveFrameButton_, &QPushButton::clicked, this, &MainWindow::saveFrame);
  connect(showRegionButton_, &QPushButton::toggled, map_, &SpatialWidget::setShowRasterRegion);
  for (auto* spin :
       {left_, right_, bottom_, top_, mpp_, rotation_, defaultPower_, powerMin_, powerMax_}) {
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::rasterSettingsChanged);
  }
  connect(autoPower_, &QCheckBox::toggled, this, &MainWindow::rasterSettingsChanged);
  connect(colorMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::rasterSettingsChanged);
  connect(colorbar_, &QCheckBox::toggled, this, &MainWindow::rasterSettingsChanged);
  connect(smoothExport_, &QCheckBox::toggled, this, &MainWindow::rasterSettingsChanged);
  connect(supersampling_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::rasterSettingsChanged);
  connect(resamplingFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::rasterSettingsChanged);
  connect(renderType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::rasterSettingsChanged);
  connect(map_, &SpatialWidget::placeRequested, this, &MainWindow::place);
  connect(map_, &SpatialWidget::buildingDesignPositionPicked, this,
          [this](const QPointF& position) {
            if (buildingShapeDialog_)
              buildingShapeDialog_->acceptPickedPosition(position);
          });
  connect(map_, &SpatialWidget::buildingDesignShapeSelected, this, [this](qulonglong id) {
    if (buildingShapeDialog_)
      buildingShapeDialog_->selectShape(std::uint64_t(id));
  });
  connect(map_, &SpatialWidget::buildingDesignRulerSelected, this, [this](qulonglong id) {
    if (buildingShapeDialog_)
      buildingShapeDialog_->selectRuler(std::uint64_t(id));
  });
  connect(map_, &SpatialWidget::interpolationRegionSelected, this, [this](const QRectF& region) {
    interpolationRegion_ = region.normalized();
    clearInterpolationPreview();
    if (localFillDialog_) {
      localFillDialog_->setRegion(interpolationRegion_);
    }
  });
  connect(map_, &SpatialWidget::edgeSelected, this, [this](int index) {
    selectedEdge_ = index;
  });
  connect(&radioModel_, &RadioTableModel::changed, this, [this] {
    dirtyA_ = true;
    invalidateInterpolationPreview(
        QStringLiteral("Type-A changed; generate a new interpolation preview."));
    refresh();
  });
  connect(&buildingModel_, &BuildingTableModel::changed, this, [this] {
    dirtyB_ = true;
    frameDirty_ = true;
    invalidateInterpolationPreview(
        QStringLiteral("Type-B changed; generate a new interpolation preview."));
    updateFrameValidity();
    refresh();
  });
  connect(&radioModel_, &RadioTableModel::visibilityChanged, this, [this] {
    refresh();
  });
  connect(&buildingModel_, &BuildingTableModel::visibilityChanged, this, [this] {
    refresh();
  });
  connect(map_, &SpatialWidget::radioSelectionChanged, this, [this](const QList<qulonglong>& ids) {
    auto* selection = radioTable_->selectionModel();
    QSignalBlocker blocker(selection);
    selection->clearSelection();
    QModelIndex first;
    for (const auto id : ids) {
      const int row = radioModel_.rowForId(static_cast<std::uint64_t>(id));
      if (row < 0) {
        continue;
      }
      const auto index = radioModel_.index(row, 0);
      selection->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
      if (!first.isValid()) {
        first = index;
      }
    }
    if (first.isValid()) {
      const int horizontal = radioTable_->horizontalScrollBar()->value();
      selection->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
      radioTable_->scrollTo(first, QAbstractItemView::PositionAtCenter);
      radioTable_->horizontalScrollBar()->setValue(horizontal);
    }
  });
  connect(map_, &SpatialWidget::buildingSelectionChanged, this,
          [this](const QList<qulonglong>& ids) {
            auto* selection = buildingTable_->selectionModel();
            QSignalBlocker blocker(selection);
            selection->clearSelection();
            QModelIndex first;
            for (const auto id : ids) {
              const int row = buildingModel_.rowForId(std::uint64_t(id));
              if (row < 0)
                continue;
              const auto index = buildingModel_.index(row, 0);
              selection->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
              if (!first.isValid())
                first = index;
            }
            if (first.isValid()) {
              const int horizontal = buildingTable_->horizontalScrollBar()->value();
              selection->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
              buildingTable_->scrollTo(first, QAbstractItemView::PositionAtCenter);
              buildingTable_->horizontalScrollBar()->setValue(horizontal);
            }
          });
  connect(radioTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
    QList<qulonglong> ids;
    for (const auto& index : radioTable_->selectionModel()->selectedRows()) {
      const auto* point = radioModel_.pointAt(index.row());
      if (point) {
        ids << qulonglong(point->id);
      }
    }
    map_->setSelectedRadios(ids);
  });
  connect(buildingTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
    QList<qulonglong> ids;
    for (const auto& index : buildingTable_->selectionModel()->selectedRows()) {
      const auto* point = buildingModel_.pointAt(index.row());
      if (point)
        ids << qulonglong(point->id);
    }
    if (!ids.isEmpty()) {
      selectedEdge_ = -1;
      map_->setSelectedEdge(std::nullopt);
    }
    map_->setSelectedBuildings(ids);
  });
  supersampling_->setEnabled(smoothExport_->isChecked());
  resamplingFilter_->setEnabled(smoothExport_->isChecked());
  colorbar_->setEnabled(renderType_->currentIndex() >= 2);
  map_->setRasterRegion(currentRegion());
  setMode();
  refresh();
}

void MainWindow::loadSettings() {
  QSettings q;
  settings_.fallback_latitude_deg = q.value("origin/lat", 0).toDouble();
  settings_.fallback_longitude_deg = q.value("origin/lon", 0).toDouble();
  settings_.fallback_altitude_m = q.value("origin/alt", 0).toDouble();
  settings_.automatic_default_power = q.value("power/auto", true).toBool();
  settings_.manual_default_power_dbfs = q.value("power/default", -120).toDouble();
  settings_.power_min_dbfs = q.value("power/min", -120).toDouble();
  settings_.power_max_dbfs = q.value("power/max", -20).toDouble();
  settings_.meters_per_pixel = q.value("raster/mpp", 1).toDouble();
  settings_.raster_left_m = q.value("raster/left", 50).toDouble();
  settings_.raster_right_m = q.value("raster/right", 50).toDouble();
  settings_.raster_bottom_m = q.value("raster/bottom", 50).toDouble();
  settings_.raster_top_m = q.value("raster/top", 50).toDouble();
  settings_.raster_rotation_deg = q.value("raster/rotation_deg", 0.0).toDouble();
  settings_.render_options.color_mode = static_cast<RasterColorMode>(
      q.value("raster/color_mode", int(RasterColorMode::Grayscale)).toInt());
  settings_.render_options.add_colorbar = q.value("raster/add_colorbar", false).toBool();
  settings_.render_options.smooth_export = q.value("raster/smooth", false).toBool();
  settings_.render_options.supersampling_factor = q.value("raster/supersampling", 4).toInt();
  settings_.render_options.resampling_filter = static_cast<RasterResamplingFilter>(
      q.value("raster/resampling_filter", int(RasterResamplingFilter::Bicubic)).toInt());
  settings_.point_size_px = q.value("display/point", 6).toInt();
  settings_.inactive_opacity = q.value("display/inactive", .22).toDouble();
}

void MainWindow::persistSettings() {
  QSettings q;
  q.setValue("origin/lat", settings_.fallback_latitude_deg);
  q.setValue("origin/lon", settings_.fallback_longitude_deg);
  q.setValue("origin/alt", settings_.fallback_altitude_m);
  q.setValue("power/auto", settings_.automatic_default_power);
  q.setValue("power/default", settings_.manual_default_power_dbfs);
  q.setValue("power/min", settings_.power_min_dbfs);
  q.setValue("power/max", settings_.power_max_dbfs);
  q.setValue("raster/mpp", settings_.meters_per_pixel);
  q.setValue("raster/left", settings_.raster_left_m);
  q.setValue("raster/right", settings_.raster_right_m);
  q.setValue("raster/bottom", settings_.raster_bottom_m);
  q.setValue("raster/top", settings_.raster_top_m);
  q.setValue("raster/rotation_deg", settings_.raster_rotation_deg);
  q.setValue("raster/color_mode", int(settings_.render_options.color_mode));
  q.setValue("raster/add_colorbar", settings_.render_options.add_colorbar);
  q.setValue("raster/smooth", settings_.render_options.smooth_export);
  q.setValue("raster/supersampling", settings_.render_options.supersampling_factor);
  q.setValue("raster/resampling_filter", int(settings_.render_options.resampling_filter));
  q.setValue("display/point", settings_.point_size_px);
  q.setValue("display/inactive", settings_.inactive_opacity);
}

QString MainWindow::normalizedPath(const QString& path) const {
  QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  QString normalized = QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
#ifdef Q_OS_WIN
  normalized = normalized.toLower();
#endif
  return normalized;
}

bool MainWindow::protectImportedSource(const QString& path) {
  if (path.isEmpty())
    return false;
  protectedSourcePaths_.insert(normalizedPath(path));
  return true;
}

bool MainWindow::canWritePath(const QString& path) {
  if (!protectedSourcePaths_.contains(normalizedPath(path)))
    return true;
  QMessageBox::warning(
      this, "Imported Source Is Read-only",
      "The selected path is an imported source file. B210RadioMapPost keeps imported files "
      "immutable; choose a different Save As destination.");
  return false;
}

bool MainWindow::coordinateModesCompatible(const RadioDataset& radio,
                                           const BuildingDataset& building, QString& error) const {
  if (radio.private_enu != building.private_enu) {
    error = QStringLiteral(
        "Standard and private coordinate files cannot be mixed because their absolute frame "
        "relationship cannot be proven.");
    return false;
  }
  if (radio.private_enu && radio.local_frame_id != building.local_frame_id) {
    error = QStringLiteral(
        "Private Type-A and Type-B have different LOCAL_FRAME_ID values and cannot be aligned.");
    return false;
  }
  return true;
}

bool MainWindow::confirmDiscard() {
  if (!dirtyA_ && !dirtyB_ && !frameDirty_)
    return true;
  return QMessageBox::question(this, "Unsaved Changes",
                               "The current project contains unsaved edits. Discard them?",
                               QMessageBox::Discard | QMessageBox::Cancel,
                               QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (buildingShapeDialog_ && !buildingShapeDialog_->close()) {
    event->ignore();
    return;
  }
  if (confirmDiscard())
    event->accept();
  else
    event->ignore();
}

bool MainWindow::resolveBuildingReference(const BuildingSource& source, EnuReference& reference,
                                          std::uint64_t& originId, QString& error) const {
  if (source.building.private_enu) {
    reference = fallback(settings_);
    originId = source.origin_point_id;
    if (originId == 0 && !source.building.points.empty())
      originId = source.building.points.front().id;
    return true;
  }
  if (source.kind == BuildingFileKind::RasterFrame && source.has_raster_frame) {
    reference = source.project_reference;
    originId = source.origin_point_id;
    return true;
  }
  for (const auto& point : source.building.points) {
    if (finiteGeo(point.latitude_deg, point.longitude_deg)) {
      reference = {point.latitude_deg, point.longitude_deg,
                   std::isfinite(point.altitude_m) ? point.altitude_m : 0, true,
                   ReferenceSource::File};
      originId = point.id;
      return true;
    }
    if (std::isfinite(point.east_m) && std::isfinite(point.north_m) &&
        source.building.source_reference.valid) {
      const auto w = CoordinateTransform::enuToWgs84(point.east_m, point.north_m,
                                                     std::isfinite(point.up_m) ? point.up_m : 0,
                                                     source.building.source_reference);
      reference = {w.latitude_deg, w.longitude_deg, w.altitude_m, true, ReferenceSource::File};
      originId = point.id;
      return true;
    }
  }
  error = QStringLiteral("Type-B has no point that can define the project origin.");
  return false;
}

void MainWindow::applyProjectReference(const EnuReference& reference) {
  if (!radio_.private_enu)
    CsvCodec::alignRadio(radio_, reference);
  if (!building_.private_enu)
    CsvCodec::alignBuilding(building_, reference);
  radio_.reference = reference;
  building_.source_reference = reference;
  frameReference_ = reference;
  buildingModel_.setDataSource(&building_, &radio_.reference);
}

void MainWindow::applyRadioOnlyReference() {
  EnuReference reference = radio_.reference;
  std::uint64_t originPointId = 0;
  if (CsvCodec::rebaseRadioToFirstRx(radio_, reference, originPointId)) {
    frameReference_ = reference;
    originPointId_ = originPointId;
  } else {
    frameReference_ = fallback(settings_);
    originPointId_ = 0;
  }
  radio_.reference = frameReference_;
  buildingModel_.setDataSource(&building_, &radio_.reference);
}

void MainWindow::applyFrameSourceSettings(const BuildingSource& source) {
  if (!source.has_raster_frame)
    return;
  updatingRasterUi_ = true;
  left_->setValue(source.region.left_m);
  right_->setValue(source.region.right_m);
  bottom_->setValue(source.region.bottom_m);
  top_->setValue(source.region.top_m);
  mpp_->setValue(source.region.meters_per_pixel);
  rotation_->setValue(source.region.rotation_deg);
  autoPower_->setChecked(false);
  defaultPower_->setValue(source.default_power_dbfs);
  powerMin_->setValue(source.power_min_dbfs);
  powerMax_->setValue(source.power_max_dbfs);
  updatingRasterUi_ = false;
  rasterSettingsChanged();
}

bool MainWindow::loadBuildingFile(const QString& path, bool fromImport) {
  invalidateInterpolationPreview(
      QStringLiteral("Building frame changed; generate a new interpolation preview."));
  BuildingSource source;
  LoadReport report;
  if (!CsvCodec::loadBuildingSource(path, source, report)) {
    frameHeld_ = false;
    building_ = {};
    buildingModel_.setDataSource(&building_, &radio_.reference);
    updateFrameUi();
    if (!fromImport)
      QMessageBox::warning(this, "Building / Frame Load Failed", report.errors.join('\n'));
    return false;
  }
  QString compatibilityError;
  if (!radio_.points.empty() &&
      !coordinateModesCompatible(radio_, source.building, compatibilityError)) {
    if (!fromImport)
      QMessageBox::warning(this, "Coordinate Frame Mismatch", compatibilityError);
    return false;
  }
  EnuReference reference;
  std::uint64_t originId = 0;
  QString error;
  if (!resolveBuildingReference(source, reference, originId, error)) {
    frameHeld_ = false;
    building_ = std::move(source.building);
    buildingModel_.setDataSource(&building_, &radio_.reference);
    updateFrameUi();
    if (!fromImport)
      QMessageBox::warning(this, "No Project Origin", error);
    return false;
  }
  building_ = std::move(source.building);
  originPointId_ = originId;
  if (source.kind == BuildingFileKind::RasterFrame ||
      source.kind == BuildingFileKind::PrivateRasterFrame)
    applyFrameSourceSettings(source);
  applyProjectReference(reference);
  const auto validation = RasterRenderer::validateBuilding(building_);
  frameHeld_ = validation.valid;
  frameDirty_ = false;
  dirtyB_ = false;
  frameFromRasterFile_ = source.kind == BuildingFileKind::RasterFrame ||
                         source.kind == BuildingFileKind::PrivateRasterFrame;
  pathB_.clear();
  framePath_.clear();
  protectImportedSource(path);
  if (building_.private_enu)
    localFrameId_ = building_.local_frame_id;
  buildingModel_.setDataSource(&building_, &radio_.reference);
  map_->setSources(&radio_, &building_);
  map_->resetView();
  updateFrameUi();
  QStringList messages = report.warnings;
  if (!validation.valid)
    messages.append(validation.errors);
  if (!messages.isEmpty())
    QMessageBox::information(this, validation.valid ? "Load Warnings" : "Invalid Building Layout",
                             messages.join('\n'));
  return true;
}

void MainWindow::importData() {
  if (!confirmDiscard())
    return;
  clearInterpolationPreview();
  ImportDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  RadioDataset imported;
  LoadReport radioReport;
  if (!CsvCodec::loadRadio(dialog.a->text(), fallback(settings_), imported, radioReport)) {
    QMessageBox::warning(this, "Type-A Import Failed", radioReport.errors.join('\n'));
    return;
  }
  radio_ = std::move(imported);
  pathA_.clear();
  protectImportedSource(dialog.a->text());
  if (radio_.private_enu)
    localFrameId_ = radio_.local_frame_id;
  else if (!frameHeld_ || !dialog.b->text().isEmpty())
    localFrameId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  dirtyA_ = false;
  QStringList warnings = radioReport.warnings;
  if (!dialog.b->text().isEmpty()) {
    if (!loadBuildingFile(dialog.b->text(), true)) {
      QMessageBox::warning(this, "Type-B / Frame Import Failed",
                           "Type-A was loaded, but the selected building/frame file could not "
                           "establish a usable project frame.");
      building_ = {};
      frameHeld_ = false;
      frameDirty_ = false;
      frameFromRasterFile_ = false;
      pathB_.clear();
      framePath_.clear();
      applyRadioOnlyReference();
    }
  } else if (frameHeld_) {
    QString compatibilityError;
    if (!coordinateModesCompatible(radio_, building_, compatibilityError)) {
      QMessageBox::warning(this, "Held Frame Mismatch", compatibilityError);
      building_ = {};
      frameHeld_ = false;
      frameDirty_ = false;
      frameFromRasterFile_ = false;
      applyRadioOnlyReference();
    } else {
      if (!radio_.private_enu)
        CsvCodec::alignRadio(radio_, frameReference_);
      radio_.reference = frameReference_;
      dirtyB_ = frameDirty_;
    }
    buildingModel_.setDataSource(&building_, &radio_.reference);
  } else {
    building_ = {};
    pathB_.clear();
    framePath_.clear();
    applyRadioOnlyReference();
    buildingModel_.setDataSource(&building_, &radio_.reference);
    dirtyB_ = false;
    frameDirty_ = false;
  }
  radioModel_.setDataset(&radio_);
  buildingModel_.setDataSource(&building_, &radio_.reference);
  map_->setSources(&radio_, &building_);
  map_->resetView();
  if (!warnings.isEmpty())
    QMessageBox::information(this, "Import Warnings", warnings.join('\n'));
  refresh();
}

void MainWindow::saveA() {
  const bool hasInterpolated =
      std::any_of(radio_.points.begin(), radio_.points.end(), [](const RadioPoint& point) {
        return point.generation_kind == PointGenerationKind::Interpolated;
      });
  if (hasInterpolated &&
      QMessageBox::question(
          this, QStringLiteral("Interpolation Metadata Compatibility"),
          QStringLiteral(
              "Standard B210RadioMap Type-A has no interpolation provenance fields. Generated "
              "points will be saved as ordinary Manual points. Continue?\n\nUse Create Private "
              "Copy "
              "to preserve fill method and batch metadata."),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  QString path = pathA_;
  if (path.isEmpty())
    path = QFileDialog::getSaveFileName(this, "Save Type-A", {}, "CSV (*.csv)");
  if (path.isEmpty())
    return;
  if (!canWritePath(path))
    return;
  QString error;
  if (!CsvCodec::saveRadio(path, radio_, &error))
    QMessageBox::warning(this, "Save Failed", error);
  else {
    pathA_ = path;
    dirtyA_ = false;
    refresh();
  }
}

void MainWindow::saveB() {
  QString path = pathB_;
  if (path.isEmpty())
    path = QFileDialog::getSaveFileName(this, "Save B210RadioMap-compatible Type-B", {},
                                        "CSV (*.csv)");
  if (path.isEmpty())
    return;
  if (!canWritePath(path))
    return;
  QString error;
  if (!CsvCodec::saveBuilding(path, building_, &error))
    QMessageBox::warning(this, "Save Failed", error);
  else {
    pathB_ = path;
    dirtyB_ = false;
    refresh();
  }
}

void MainWindow::saveFrame() {
  const auto validation = RasterRenderer::validateBuilding(building_);
  if (!validation.valid) {
    QMessageBox::warning(this, "Cannot Save Frame", validation.errors.join('\n'));
    return;
  }
  QString frameError;
  RasterRenderer::buildFrame(currentRegion(), &frameError);
  if (!frameError.isEmpty()) {
    QMessageBox::warning(this, "Invalid Raster Region", frameError);
    return;
  }
  QString path = framePath_;
  if (path.isEmpty())
    path = QFileDialog::getSaveFileName(this, "Save Raster Frame", {}, "Raster Frame CSV (*.csv)");
  if (path.isEmpty())
    return;
  if (!canWritePath(path))
    return;
  QString error;
  const bool saved =
      building_.private_enu
          ? CsvCodec::savePrivateRasterFrame(
                path, building_, building_.local_frame_id, originPointId_, currentRegion(),
                settings_.power_min_dbfs, settings_.power_max_dbfs, defaultPower(), &error)
          : CsvCodec::saveRasterFrame(path, building_, frameReference_, originPointId_,
                                      currentRegion(), settings_.power_min_dbfs,
                                      settings_.power_max_dbfs, defaultPower(), &error);
  if (!saved)
    QMessageBox::warning(this, "Save Frame Failed", error);
  else {
    framePath_ = path;
    frameFromRasterFile_ = true;
    frameHeld_ = true;
    frameDirty_ = false;
    updateFrameUi();
    refresh();
  }
}

void MainWindow::createPrivateCopy() {
  if (radio_.points.empty() && building_.points.empty()) {
    QMessageBox::information(this, "Nothing To Export", "Import data before creating a copy.");
    return;
  }
  const QString directory = QFileDialog::getExistingDirectory(
      this, "Choose Directory For Private Copies", QString(), QFileDialog::ShowDirsOnly);
  if (directory.isEmpty())
    return;

  QString frameId = localFrameId_;
  if (radio_.private_enu)
    frameId = radio_.local_frame_id;
  else if (building_.private_enu)
    frameId = building_.local_frame_id;
  if (frameId.isEmpty())
    frameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localFrameId_ = frameId;

  QDir output(directory);
  QStringList paths;
  if (!radio_.points.empty())
    paths << output.filePath("private_type_a.csv");
  if (!building_.points.empty())
    paths << output.filePath("private_type_b.csv");
  const auto validation = RasterRenderer::validateBuilding(building_);
  if (validation.valid)
    paths << output.filePath("private_raster_frame.csv");
  for (const auto& path : paths) {
    if (!canWritePath(path))
      return;
  }
  QStringList existing;
  for (const auto& path : paths)
    if (QFileInfo::exists(path))
      existing << QFileInfo(path).fileName();
  if (!existing.isEmpty() &&
      QMessageBox::question(
          this, "Replace Existing Private Copies",
          QStringLiteral("Replace these generated files?\n%1").arg(existing.join('\n')),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }

  QString error;
  QStringList written;
  if (!radio_.points.empty()) {
    const QString path = output.filePath("private_type_a.csv");
    if (!CsvCodec::savePrivateRadio(path, radio_, frameId, &error)) {
      QMessageBox::warning(this, "Private Export Failed", error);
      return;
    }
    written << QFileInfo(path).fileName();
  }
  if (!building_.points.empty() || !building_.edges.empty()) {
    const QString path = output.filePath("private_type_b.csv");
    if (!CsvCodec::savePrivateBuilding(path, building_, frameId, &error)) {
      QMessageBox::warning(this, "Private Export Failed", error);
      return;
    }
    written << QFileInfo(path).fileName();
  }
  if (validation.valid) {
    const QString path = output.filePath("private_raster_frame.csv");
    if (!CsvCodec::savePrivateRasterFrame(path, building_, frameId, originPointId_, currentRegion(),
                                          settings_.power_min_dbfs, settings_.power_max_dbfs,
                                          defaultPower(), &error)) {
      QMessageBox::warning(this, "Private Export Failed", error);
      return;
    }
    written << QFileInfo(path).fileName();
  }
  QMessageBox::information(
      this, "Private Copies Created",
      QStringLiteral("Removed all WGS84 values and geographic origins. ENU geometry was copied "
                     "without reprojection.\n\nLOCAL_FRAME_ID: %1\n%2")
          .arg(frameId, written.join('\n')));
}

void MainWindow::exportSyntheticWgs84Copies() {
  const bool compatiblePrivateData = !radio_.points.empty() && !building_.points.empty() &&
                                     radio_.private_enu && building_.private_enu &&
                                     !radio_.local_frame_id.trimmed().isEmpty() &&
                                     radio_.local_frame_id == building_.local_frame_id;
  if (!compatiblePrivateData) {
    QMessageBox::information(this, "Synthetic WGS84 Export Unavailable",
                             "Load non-empty Private Type-A and Private Type-B files with the same "
                             "LOCAL_FRAME_ID first.");
    return;
  }

  RadioDataset radioCopy;
  BuildingDataset buildingCopy;
  QString error;
  const EnuReference reference = fallback(settings_);
  if (!CsvCodec::createSyntheticWgs84Copies(radio_, building_, reference, radioCopy, buildingCopy,
                                            &error)) {
    QMessageBox::warning(this, "Synthetic WGS84 Conversion Failed", error);
    return;
  }

  const QString directory = QFileDialog::getExistingDirectory(
      this, "Choose Directory For Synthetic WGS84 Copies", QString(), QFileDialog::ShowDirsOnly);
  if (directory.isEmpty())
    return;

  QDir output(directory);
  const QString typeAPath = output.filePath(QStringLiteral("synthetic_wgs84_type_a.csv"));
  const QString typeBPath = output.filePath(QStringLiteral("synthetic_wgs84_type_b.csv"));
  const bool includeRasterFrame = frameHeld_ && frameFromRasterFile_;
  const QString rasterFramePath =
      output.filePath(QStringLiteral("synthetic_wgs84_raster_frame.csv"));
  QStringList paths{typeAPath, typeBPath};
  if (includeRasterFrame)
    paths << rasterFramePath;
  for (const auto& path : paths) {
    if (!canWritePath(path))
      return;
  }

  QStringList existing;
  for (const auto& path : paths) {
    if (QFileInfo::exists(path))
      existing << QFileInfo(path).fileName();
  }
  if (!existing.isEmpty() &&
      QMessageBox::question(
          this, "Replace Existing Synthetic WGS84 Copies",
          QStringLiteral("Replace these generated files?\n%1").arg(existing.join('\n')),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }

  QStringList written;
  if (!CsvCodec::saveRadio(typeAPath, radioCopy, &error)) {
    QMessageBox::warning(this, "Synthetic WGS84 Export Failed", error);
    return;
  }
  written << QFileInfo(typeAPath).fileName();
  if (!CsvCodec::saveBuilding(typeBPath, buildingCopy, &error)) {
    QMessageBox::warning(
        this, "Synthetic WGS84 Export Partially Failed",
        QStringLiteral("Written:\n%1\n\nFailure: %2").arg(written.join('\n'), error));
    return;
  }
  written << QFileInfo(typeBPath).fileName();
  if (includeRasterFrame &&
      !CsvCodec::saveRasterFrame(rasterFramePath, buildingCopy, reference, originPointId_,
                                 currentRegion(), settings_.power_min_dbfs,
                                 settings_.power_max_dbfs, defaultPower(), &error)) {
    QMessageBox::warning(
        this, "Synthetic WGS84 Export Partially Failed",
        QStringLiteral("Written:\n%1\n\nFailure: %2").arg(written.join('\n'), error));
    return;
  }
  if (includeRasterFrame)
    written << QFileInfo(rasterFramePath).fileName();

  lastExportStatus_ = QStringLiteral("Last export: Synthetic WGS84 copies");
  refresh();
  const bool omittedInterpolationMetadata =
      std::any_of(radioCopy.points.begin(), radioCopy.points.end(), [](const RadioPoint& point) {
        return point.generation_kind == PointGenerationKind::Interpolated;
      });
  const QString compatibilityNote =
      omittedInterpolationMetadata
          ? QStringLiteral(
                "\n\nNote: Standard Type-A does not preserve interpolation "
                "method or batch metadata.")
          : QString();
  QMessageBox::information(
      this, "Synthetic WGS84 Copies Created",
      QStringLiteral(
          "Generated WGS84 coordinates from Private ENU using the Settings anchor. The loaded "
          "data remains Private ENU.\n\nAnchor: %1, %2, %3 m\n\n%4%5")
          .arg(reference.latitude_deg, 0, 'g', std::numeric_limits<double>::max_digits10)
          .arg(reference.longitude_deg, 0, 'g', std::numeric_limits<double>::max_digits10)
          .arg(reference.altitude_m, 0, 'g', std::numeric_limits<double>::max_digits10)
          .arg(written.join('\n'), compatibilityNote));
}

void MainWindow::loadOrReleaseFrame() {
  invalidateInterpolationPreview(
      QStringLiteral("Building frame changed; generate a new interpolation preview."));
  if (frameHeld_) {
    if ((dirtyB_ || frameDirty_) &&
        QMessageBox::question(
            this, "Release Frame", "Release the held frame and discard its unsaved changes?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      return;
    building_ = {};
    pathB_.clear();
    framePath_.clear();
    originPointId_ = 0;
    frameHeld_ = false;
    frameDirty_ = false;
    frameFromRasterFile_ = false;
    applyRadioOnlyReference();
    buildingModel_.setDataSource(&building_, &radio_.reference);
    map_->setSources(&radio_, &building_);
    map_->resetView();
    updateFrameUi();
    refresh();
    return;
  }
  const auto path =
      QFileDialog::getOpenFileName(this, "Load Type-B or Raster Frame", {}, "CSV (*.csv)");
  if (path.isEmpty())
    return;
  if (!loadBuildingFile(path, false)) {
    applyRadioOnlyReference();
    pathB_.clear();
    framePath_.clear();
    frameFromRasterFile_ = false;
    buildingModel_.setDataSource(&building_, &radio_.reference);
    map_->setSources(&radio_, &building_);
    map_->resetView();
  }
  refresh();
}

void MainWindow::applySyntheticReference(const EnuReference& reference) {
  frameReference_ = reference;
  radio_.reference = reference;
  radio_.reference_was_synthetic = true;
  if (radio_.private_enu) {
    radioModel_.refresh();
    return;
  }
  for (auto& p : radio_.points)
    if (std::isfinite(p.east_m) && std::isfinite(p.north_m)) {
      const auto w = CoordinateTransform::enuToWgs84(p.east_m, p.north_m,
                                                     std::isfinite(p.up_m) ? p.up_m : 0, reference);
      p.latitude_deg = w.latitude_deg;
      p.longitude_deg = w.longitude_deg;
      p.altitude_m = w.altitude_m;
      p.geo_status = GeoStatus::SyntheticWgs84;
    }
  radioModel_.refresh();
}

void MainWindow::editSettings() {
  SettingsDialog dialog(settings_, this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  const auto old = fallback(settings_);
  settings_ = dialog.settings();
  const auto updated = fallback(settings_);
  persistSettings();
  if (!frameHeld_ && radio_.reference_was_synthetic &&
      (old.latitude_deg != updated.latitude_deg || old.longitude_deg != updated.longitude_deg ||
       old.altitude_m != updated.altitude_m))
    applySyntheticReference(updated);
  map_->setSettings(settings_);
  refresh();
}

void MainWindow::setMode() {
  const auto layer = static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt());
  map_->setEditLayer(layer);
  placing_ = false;
  placeButton_->setChecked(false);
  map_->setPlacement(false);
  const bool radio = layer == SpatialWidget::EditLayer::Radio;
  localFillButton_->setEnabled(radio);
  localFillButton_->setVisible(radio);
  buildingShapeButton_->setEnabled(!radio);
  buildingShapeButton_->setVisible(!radio);
  toggleRoleButton_->setVisible(radio);
  connectPointsButton_->setVisible(!radio);
  deleteEdgeButton_->setVisible(!radio);
  if (!radio) {
    map_->cancelInterpolationRegionSelection();
    clearInterpolationPreview();
    if (localFillDialog_) {
      localFillDialog_->hide();
    }
  } else if (buildingShapeDialog_) {
    buildingShapeDialog_->close();
  }
  placeButton_->setText(radio ? "Place Power Point" : "Place Building Point");
  placeButton_->setToolTip(radio ? "Place a manual RX point using the resolved default power."
                                 : "Place a Type-B vertex in the project ENU frame.");
  radioTable_->setEditTriggers(radio ? QAbstractItemView::DoubleClicked |
                                           QAbstractItemView::EditKeyPressed
                                     : QAbstractItemView::NoEditTriggers);
  buildingTable_->setEditTriggers(!radio ? QAbstractItemView::DoubleClicked |
                                               QAbstractItemView::EditKeyPressed
                                         : QAbstractItemView::NoEditTriggers);
}

void MainWindow::openBuildingShapeEditor() {
  if (static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt()) !=
      SpatialWidget::EditLayer::Building) {
    QMessageBox::information(this, QStringLiteral("Building Mode Required"),
                             QStringLiteral("Switch to Edit Building GNSS first."));
    return;
  }
  if (buildingShapeDialog_) {
    buildingShapeDialog_->show();
    buildingShapeDialog_->raise();
    buildingShapeDialog_->activateWindow();
    return;
  }
  const bool hasExistingGeometry = !building_.points.empty() || !building_.edges.empty();
  const bool existingGeometryClosed =
      hasExistingGeometry && RasterRenderer::validateBuilding(building_).valid;
  placing_ = false;
  placeButton_->setChecked(false);
  map_->setPlacement(false);
  BuildingShapeDialog::Callbacks callbacks;
  callbacks.update_overlay = [this](const BuildingDesignOverlay& overlay) {
    map_->setBuildingDesignOverlay(overlay);
  };
  callbacks.set_pick_mode = [this](BuildingDesignPickMode mode) {
    map_->setBuildingDesignPickMode(mode);
  };
  callbacks.commit = [this](const std::vector<BuildingShapeSnapshot>& snapshots, QString& error) {
    return commitBuildingShapes(snapshots, error);
  };
  callbacks.clear_overlay = [this] {
    map_->clearBuildingDesignOverlay();
  };
  buildingShapeDialog_ = new BuildingShapeDialog(std::move(callbacks), this);
  if (!hasExistingGeometry) {
    buildingShapeDialog_->setSourceNotice(
        QStringLiteral("No Type-B geometry is currently loaded. Applied shapes will create a new "
                       "closed building layout."));
  } else if (existingGeometryClosed) {
    buildingShapeDialog_->setSourceNotice(
        QStringLiteral("The current Type-B layout is already closed. Applied shapes will be "
                       "appended as additional closed building contours."));
  } else {
    buildingShapeDialog_->setSourceNotice(QStringLiteral(
        "The current Type-B contains unclosed GNSS points/edges. They remain visible as placement "
        "references while this window is open. Applying one or more shapes will replace that "
        "unclosed geometry with the generated closed contours. Closing without placing a shape "
        "leaves the Type-B data unchanged."));
  }
  buildingShapeDialog_->show();
}

bool MainWindow::commitBuildingShapes(const std::vector<BuildingShapeSnapshot>& snapshots,
                                      QString& error) {
  error.clear();
  if (snapshots.empty())
    return true;
  if (!radio_.reference.valid) {
    radio_.reference = fallback(settings_);
    frameReference_ = radio_.reference;
  }
  const bool hasExistingGeometry = !building_.points.empty() || !building_.edges.empty();
  const bool replaceUnclosedGeometry =
      hasExistingGeometry && !RasterRenderer::validateBuilding(building_).valid;
  BuildingDataset candidate = building_;
  if (replaceUnclosedGeometry) {
    candidate.points.clear();
    candidate.edges.clear();
  }
  if (candidate.points.empty()) {
    if (!hasExistingGeometry) {
      candidate.private_enu = radio_.private_enu;
      candidate.local_frame_id = radio_.local_frame_id;
    }
    candidate.source_reference = radio_.reference;
  }
  std::uint64_t nextId = 0;
  for (const auto& point : candidate.points)
    nextId = std::max(nextId, point.id);

  std::size_t totalNewPoints = 0;
  for (const auto& snapshot : snapshots) {
    const auto shape = createBuildingShape(snapshot);
    totalNewPoints += shape->discretize().size();
    if (totalNewPoints > 1000000) {
      error = QStringLiteral("The requested discretization exceeds 1,000,000 new points.");
      return false;
    }
  }
  if (totalNewPoints > std::numeric_limits<std::uint64_t>::max() - nextId) {
    error = QStringLiteral("Building point IDs would overflow.");
    return false;
  }
  candidate.points.reserve(candidate.points.size() + totalNewPoints);
  candidate.edges.reserve(candidate.edges.size() + totalNewPoints);
  for (const auto& snapshot : snapshots) {
    const auto shape = createBuildingShape(snapshot);
    const auto positions = shape->discretize();
    if (positions.size() < 3) {
      error = QStringLiteral("Every building outline must contain at least three points.");
      return false;
    }
    std::vector<std::uint64_t> ids;
    ids.reserve(positions.size());
    for (const auto& position : positions) {
      SurveyPoint point;
      point.id = ++nextId;
      point.east_m = position.x();
      point.north_m = position.y();
      point.up_m = 0.0;
      point.origin = DataOrigin::Manual;
      point.edited = true;
      point.visible = true;
      if (candidate.private_enu) {
        point.geo_status = GeoStatus::PrivateEnu;
      } else {
        const auto wgs = CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m,
                                                         radio_.reference);
        point.latitude_deg = wgs.latitude_deg;
        point.longitude_deg = wgs.longitude_deg;
        point.altitude_m = wgs.altitude_m;
        point.geo_status = radio_.reference.source == ReferenceSource::File
                               ? GeoStatus::DerivedFromFileReference
                               : GeoStatus::SyntheticWgs84;
      }
      ids.push_back(point.id);
      candidate.points.push_back(std::move(point));
    }
    for (std::size_t index = 0; index < ids.size(); ++index)
      candidate.edges.push_back({ids[index], ids[(index + 1) % ids.size()]});
  }
  const auto validation = RasterRenderer::validateBuilding(candidate);
  if (!validation.valid) {
    error = validation.errors.join('\n');
    return false;
  }
  invalidateInterpolationPreview(
      QStringLiteral("Building data changed; generate a new interpolation preview."));
  building_ = std::move(candidate);
  buildingModel_.refresh();
  dirtyB_ = true;
  frameDirty_ = true;
  updateFrameValidity();
  refresh();
  return true;
}

const InterpolationMethod* MainWindow::interpolationMethod(const QString& id) const {
  for (const auto& method : interpolationMethods_) {
    if (method->id() == id) {
      return method.get();
    }
  }
  return nullptr;
}

void MainWindow::openLocalFill() {
  if (static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt()) !=
      SpatialWidget::EditLayer::Radio) {
    QMessageBox::information(
        this, QStringLiteral("Power / GNSS Mode Required"),
        QStringLiteral("Switch to Edit Power / GNSS before using Local Fill."));
    return;
  }
  if (localFillDialog_) {
    localFillDialog_->show();
    localFillDialog_->raise();
    localFillDialog_->activateWindow();
    return;
  }
  LocalFillDialog::Callbacks callbacks;
  callbacks.begin_region_selection = [this] {
    beginInterpolationRegionSelection();
  };
  callbacks.preview = [this](const QString& methodId, const QJsonObject& parameters,
                             const QRectF& region) {
    previewInterpolation(methodId, parameters, region);
  };
  callbacks.commit = [this] {
    commitInterpolation();
  };
  callbacks.clear_preview = [this] {
    clearInterpolationPreview();
  };
  localFillDialog_ = new LocalFillDialog(std::move(callbacks), this);
  localFillDialog_->show();
}

void MainWindow::beginInterpolationRegionSelection() {
  if (placing_) {
    placing_ = false;
    placeButton_->setChecked(false);
  }
  clearInterpolationPreview();
  map_->beginInterpolationRegionSelection();
  status_->setText(QStringLiteral("Drag a local interpolation rectangle on the ENU map."));
}

void MainWindow::previewInterpolation(const QString& methodId, const QJsonObject& parameters,
                                      const QRectF& region) {
  const auto* method = interpolationMethod(methodId);
  if (!method) {
    QMessageBox::warning(this, QStringLiteral("Interpolation Module Missing"),
                         QStringLiteral("The selected interpolation module is not registered."));
    return;
  }
  QString error;
  if (!method->validateParameters(parameters, &error)) {
    QMessageBox::warning(this, QStringLiteral("Invalid Parameters"), error);
    return;
  }
  interpolationRegion_ = region.normalized();
  InterpolationRequest request;
  request.radio = &radio_;
  request.building = &building_;
  request.region = interpolationRegion_;
  request.parameters = parameters;
  interpolationPreview_ = method->interpolate(request);
  interpolationPreviewMethodId_ = methodId;
  interpolationPreviewParameters_ = parameters;
  map_->setInterpolationPreview(interpolationRegion_, interpolationPreview_.samples);

  QStringList summary;
  summary << QStringLiteral("Generated: %1 / %2 candidates")
                 .arg(qulonglong(interpolationPreview_.samples.size()))
                 .arg(interpolationPreview_.candidate_count)
          << QStringLiteral(
                 "Skipped — near measured: %1, building: %2, insufficient support: %3, "
                 "barrier: %4, uncertainty: %5")
                 .arg(interpolationPreview_.skipped_near_existing)
                 .arg(interpolationPreview_.skipped_building)
                 .arg(interpolationPreview_.skipped_support)
                 .arg(interpolationPreview_.skipped_barrier)
                 .arg(interpolationPreview_.skipped_uncertainty);
  const auto validation = RasterRenderer::validateBuilding(building_);
  if (!building_.points.empty() && !validation.valid) {
    summary << QStringLiteral(
        "Warning: Type-B is not a valid closed layout, so building exclusion was unavailable.");
  } else if (building_.points.empty()) {
    summary << QStringLiteral(
        "Warning: no Type-B layout is loaded; no building exclusion was applied.");
  }
  summary.append(interpolationPreview_.warnings);
  if (localFillDialog_) {
    localFillDialog_->setPreviewSummary(summary.join('\n'), !interpolationPreview_.samples.empty());
  }
}

void MainWindow::commitInterpolation() {
  if (!localFillDialog_ || interpolationPreview_.samples.empty()) {
    return;
  }
  if (localFillDialog_->currentMethodId() != interpolationPreviewMethodId_ ||
      localFillDialog_->currentParameters() != interpolationPreviewParameters_) {
    invalidateInterpolationPreview(
        QStringLiteral("Parameters changed; generate a new preview before confirming."));
    QMessageBox::information(
        this, QStringLiteral("Preview Is Stale"),
        QStringLiteral("Interpolation parameters changed after the preview. Preview again "
                       "before appending points."));
    return;
  }

  std::uint64_t nextId = 0;
  std::uint64_t nextSequence = 0;
  std::uint64_t nextBatch = 0;
  double upSum = 0.0;
  int upCount = 0;
  for (const auto& point : radio_.points) {
    nextId = std::max(nextId, point.id);
    nextSequence = std::max(nextSequence, point.rf_sequence);
    nextBatch = std::max(nextBatch, point.interpolation_batch_id);
    if (point.role == RadioRole::Rx && point.valid &&
        point.generation_kind != PointGenerationKind::Interpolated &&
        interpolationRegion_.contains(point.east_m, point.north_m) && std::isfinite(point.up_m)) {
      upSum += point.up_m;
      ++upCount;
    }
  }
  ++nextBatch;
  const double generatedUp = upCount > 0 ? upSum / upCount : 0.0;
  radio_.points.reserve(radio_.points.size() + interpolationPreview_.samples.size());
  for (const auto& sample : interpolationPreview_.samples) {
    RadioPoint point;
    point.id = ++nextId;
    point.rf_sequence = ++nextSequence;
    point.east_m = sample.position.x();
    point.north_m = sample.position.y();
    point.up_m = generatedUp;
    point.power_dbfs = sample.power_dbfs;
    point.role = RadioRole::Rx;
    point.origin = DataOrigin::Manual;
    point.generation_kind = PointGenerationKind::Interpolated;
    point.interpolation_batch_id = nextBatch;
    point.interpolation_method = interpolationPreviewMethodId_;
    point.valid = true;
    point.edited = true;
    point.visible = true;
    if (radio_.private_enu) {
      point.geo_status = GeoStatus::PrivateEnu;
    } else {
      const auto wgs = CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m,
                                                       radio_.reference);
      point.latitude_deg = wgs.latitude_deg;
      point.longitude_deg = wgs.longitude_deg;
      point.altitude_m = wgs.altitude_m;
      point.geo_status = radio_.reference.source == ReferenceSource::File
                             ? GeoStatus::DerivedFromFileReference
                             : GeoStatus::SyntheticWgs84;
    }
    radio_.points.push_back(std::move(point));
  }
  const std::size_t appended = interpolationPreview_.samples.size();
  radioModel_.refresh();
  dirtyA_ = true;
  clearInterpolationPreview();
  if (localFillDialog_) {
    localFillDialog_->invalidatePreview(
        QStringLiteral("Appended %1 interpolated RX points as batch %2.")
            .arg(qulonglong(appended))
            .arg(qulonglong(nextBatch)));
  }
  refresh();
}

void MainWindow::clearInterpolationPreview() {
  interpolationPreview_ = {};
  interpolationPreviewMethodId_.clear();
  interpolationPreviewParameters_ = {};
  map_->clearInterpolationPreview();
}

void MainWindow::invalidateInterpolationPreview(const QString& reason) {
  if (interpolationPreview_.samples.empty()) {
    return;
  }
  clearInterpolationPreview();
  if (localFillDialog_) {
    localFillDialog_->invalidatePreview(reason);
  }
}

void MainWindow::togglePlace() {
  placing_ = placeButton_->isChecked();
  map_->setPlacement(placing_);
}

double MainWindow::defaultPower() const {
  if (!settings_.automatic_default_power)
    return settings_.manual_default_power_dbfs;
  double value = std::numeric_limits<double>::infinity();
  for (const auto& p : radio_.points)
    if (p.role == RadioRole::Rx && p.valid && std::isfinite(p.power_dbfs))
      value = std::min(value, p.power_dbfs);
  return std::isfinite(value) ? value : settings_.manual_default_power_dbfs;
}

void MainWindow::place(double east, double north) {
  invalidateInterpolationPreview(
      QStringLiteral("Point data changed; generate a new interpolation preview."));
  if (!radio_.reference.valid) {
    radio_.reference = fallback(settings_);
    frameReference_ = radio_.reference;
  }
  const auto layer = static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt());
  if (layer == SpatialWidget::EditLayer::Radio) {
    RadioPoint point;
    for (const auto& q : radio_.points) {
      point.id = std::max(point.id, q.id);
      point.rf_sequence = std::max(point.rf_sequence, q.rf_sequence);
    }
    ++point.id;
    ++point.rf_sequence;
    point.east_m = east;
    point.north_m = north;
    point.up_m = 0;
    point.power_dbfs = defaultPower();
    point.origin = DataOrigin::Manual;
    point.generation_kind = PointGenerationKind::Manual;
    point.edited = true;
    point.valid = true;
    if (radio_.private_enu) {
      point.geo_status = GeoStatus::PrivateEnu;
    } else {
      const auto w = CoordinateTransform::enuToWgs84(east, north, 0, radio_.reference);
      point.latitude_deg = w.latitude_deg;
      point.longitude_deg = w.longitude_deg;
      point.altitude_m = w.altitude_m;
      point.geo_status = radio_.reference.source == ReferenceSource::File
                             ? GeoStatus::DerivedFromFileReference
                             : GeoStatus::SyntheticWgs84;
    }
    radio_.points.push_back(point);
    radioModel_.refresh();
    dirtyA_ = true;
  } else {
    if (building_.points.empty()) {
      building_.private_enu = radio_.private_enu;
      building_.local_frame_id = radio_.local_frame_id;
      building_.source_reference = radio_.reference;
    }
    SurveyPoint point;
    for (const auto& q : building_.points)
      point.id = std::max(point.id, q.id);
    ++point.id;
    point.east_m = east;
    point.north_m = north;
    point.up_m = 0;
    point.origin = DataOrigin::Manual;
    point.edited = true;
    if (building_.private_enu) {
      point.geo_status = GeoStatus::PrivateEnu;
    } else {
      const auto w = CoordinateTransform::enuToWgs84(east, north, 0, radio_.reference);
      point.latitude_deg = w.latitude_deg;
      point.longitude_deg = w.longitude_deg;
      point.altitude_m = w.altitude_m;
      point.geo_status = radio_.reference.source == ReferenceSource::File
                             ? GeoStatus::DerivedFromFileReference
                             : GeoStatus::SyntheticWgs84;
    }
    building_.points.push_back(point);
    buildingModel_.refresh();
    dirtyB_ = true;
  }
  refresh();
}

void MainWindow::deleteSelected() {
  const auto layer = static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt());
  if (layer == SpatialWidget::EditLayer::Radio) {
    const auto rows = radioTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
      return;
    }
    if (QMessageBox::question(this, "Delete Type-A Points",
                              QString("Delete %1 selected Type-A point(s)?").arg(rows.size()),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
    invalidateInterpolationPreview(
        QStringLiteral("Point data changed; generate a new interpolation preview."));
    std::vector<std::uint64_t> ids;
    ids.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& index : rows) {
      ids.push_back(radio_.points[static_cast<std::size_t>(index.row())].id);
    }
    for (const auto id : ids) {
      const auto found =
          std::find_if(radio_.points.begin(), radio_.points.end(), [id](const auto& point) {
            return point.id == id;
          });
      if (found != radio_.points.end()) {
        radio_.points.erase(found);
      }
    }
    radioTable_->selectionModel()->clearSelection();
    map_->setSelectedRadios({});
    radioModel_.refresh();
    dirtyA_ = true;
  } else {
    const auto rows = buildingTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
      return;
    }
    if (QMessageBox::question(
            this, "Delete Type-B Points",
            QString("Delete %1 selected Type-B point(s) and their incident edges?")
                .arg(rows.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
    invalidateInterpolationPreview(
        QStringLiteral("Building data changed; generate a new interpolation preview."));
    std::vector<std::uint64_t> ids;
    ids.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& index : rows) {
      ids.push_back(building_.points[static_cast<std::size_t>(index.row())].id);
    }
    for (const auto id : ids) {
      const auto found =
          std::find_if(building_.points.begin(), building_.points.end(), [id](const auto& point) {
            return point.id == id;
          });
      if (found != building_.points.end()) {
        building_.points.erase(found);
      }
      building_.edges.erase(std::remove_if(building_.edges.begin(), building_.edges.end(),
                                           [id](const auto& edge) {
                                             return edge.a == id || edge.b == id;
                                           }),
                            building_.edges.end());
    }
    buildingTable_->selectionModel()->clearSelection();
    map_->setSelectedBuildings({});
    buildingModel_.refresh();
    dirtyB_ = true;
    frameDirty_ = true;
    updateFrameValidity();
  }
  refresh();
}

void MainWindow::deleteEdge() {
  if (selectedEdge_ < 0 || selectedEdge_ >= int(building_.edges.size()))
    return;
  invalidateInterpolationPreview(
      QStringLiteral("Building data changed; generate a new interpolation preview."));
  building_.edges.erase(building_.edges.begin() + selectedEdge_);
  selectedEdge_ = -1;
  map_->setSelectedEdge(std::nullopt);
  dirtyB_ = true;
  frameDirty_ = true;
  updateFrameValidity();
  refresh();
}

void MainWindow::toggleRadioRole() {
  if (static_cast<SpatialWidget::EditLayer>(mode_->currentData().toInt()) !=
      SpatialWidget::EditLayer::Radio)
    return;
  const auto rows = radioTable_->selectionModel()->selectedRows();
  if (rows.isEmpty()) {
    return;
  }
  for (const auto& index : rows) {
    const int row = index.row();
    const auto* point = radioModel_.pointAt(row);
    if (!point) {
      continue;
    }
    radioModel_.setData(radioModel_.index(row, 2),
                        radioRoleName(point->role == RadioRole::Rx ? RadioRole::Tx : RadioRole::Rx),
                        Qt::EditRole);
  }
}

void MainWindow::addEdge() {
  const auto rows = buildingTable_->selectionModel()->selectedRows();
  if (rows.size() != 2) {
    QMessageBox::information(
        this, "Select Two Points",
        "Select exactly two Type-B points. Ctrl-click on the map is supported.");
    return;
  }
  const auto a = building_.points[size_t(rows[0].row())].id,
             b = building_.points[size_t(rows[1].row())].id;
  if (a == b)
    return;
  for (const auto& edge : building_.edges)
    if ((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a)) {
      QMessageBox::information(this, "Duplicate Edge", "These points are already connected.");
      return;
    }
  invalidateInterpolationPreview(
      QStringLiteral("Building data changed; generate a new interpolation preview."));
  building_.edges.push_back({a, b});
  dirtyB_ = true;
  frameDirty_ = true;
  updateFrameValidity();
  refresh();
}

RasterRegion MainWindow::currentRegion() const {
  return {left_->value(), right_->value(), bottom_->value(),
          top_->value(),  mpp_->value(),   rotation_->value()};
}

void MainWindow::rasterSettingsChanged() {
  if (updatingRasterUi_)
    return;
  const bool frameDefinitionChanged =
      settings_.raster_left_m != left_->value() || settings_.raster_right_m != right_->value() ||
      settings_.raster_bottom_m != bottom_->value() || settings_.raster_top_m != top_->value() ||
      settings_.meters_per_pixel != mpp_->value() ||
      settings_.raster_rotation_deg != rotation_->value() ||
      settings_.automatic_default_power != autoPower_->isChecked() ||
      settings_.manual_default_power_dbfs != defaultPower_->value() ||
      settings_.power_min_dbfs != powerMin_->value() ||
      settings_.power_max_dbfs != powerMax_->value();
  settings_.raster_left_m = left_->value();
  settings_.raster_right_m = right_->value();
  settings_.raster_bottom_m = bottom_->value();
  settings_.raster_top_m = top_->value();
  settings_.meters_per_pixel = mpp_->value();
  settings_.raster_rotation_deg = rotation_->value();
  settings_.automatic_default_power = autoPower_->isChecked();
  settings_.manual_default_power_dbfs = defaultPower_->value();
  settings_.power_min_dbfs = powerMin_->value();
  settings_.power_max_dbfs = powerMax_->value();
  settings_.render_options.color_mode =
      static_cast<RasterColorMode>(colorMode_->currentData().toInt());
  settings_.render_options.add_colorbar = colorbar_->isChecked();
  settings_.render_options.smooth_export = smoothExport_->isChecked();
  settings_.render_options.supersampling_factor = supersampling_->currentData().toInt();
  settings_.render_options.resampling_filter =
      static_cast<RasterResamplingFilter>(resamplingFilter_->currentData().toInt());
  supersampling_->setEnabled(smoothExport_->isChecked());
  resamplingFilter_->setEnabled(smoothExport_->isChecked());
  colorbar_->setEnabled(renderType_->currentIndex() >= 2);
  if (settings_.power_max_dbfs <= settings_.power_min_dbfs)
    powerMax_->setStyleSheet("background:#ffcccc");
  else
    powerMax_->setStyleSheet(QString());
  persistSettings();
  map_->setSettings(settings_);
  map_->setRasterRegion(currentRegion());
  if (frameHeld_ && frameDefinitionChanged)
    frameDirty_ = true;
  updateFrameUi();
  refresh();
}

void MainWindow::updateFrameValidity() {
  const auto validation = RasterRenderer::validateBuilding(building_);
  if (!validation.valid)
    frameHeld_ = false;
  else if (!building_.points.empty())
    frameHeld_ = true;
  updateFrameUi();
}

void MainWindow::updateFrameUi() {
  QString frameError;
  const auto frame = RasterRenderer::buildFrame(currentRegion(), &frameError);
  const auto validation = RasterRenderer::validateBuilding(building_);
  if (frameHeld_ && validation.valid) {
    frameButton_->setText("HOLD FRAME");
    frameButton_->setStyleSheet("background:#2e8b57;color:white;font-weight:bold;");
  } else {
    frameButton_->setText("LOAD FRAME");
    frameButton_->setStyleSheet("background:#f0a030;color:black;font-weight:bold;");
  }
  saveFrameButton_->setEnabled(validation.valid && frameError.isEmpty());
  QString source = QStringLiteral("none");
  if (frameHeld_) {
    source =
        frameFromRasterFile_ ? QStringLiteral("Raster Frame CSV") : QStringLiteral("Type-B CSV");
    if (building_.private_enu)
      source.prepend(QStringLiteral("Private "));
  }
  QString dimensions = frameError.isEmpty()
                           ? QString("%1 × %2 m | %3 × %4 px | rotation %5° CCW")
                                 .arg(frame.max_east_m - frame.min_east_m, 0, 'f', 3)
                                 .arg(frame.max_north_m - frame.min_north_m, 0, 'f', 3)
                                 .arg(frame.width_px)
                                 .arg(frame.height_px)
                                 .arg(frame.rotation_deg, 0, 'f', 2)
                           : frameError;
  QString origin = building_.private_enu   ? QString("private ENU origin, point ID=%1, frame=%2")
                                                 .arg(qulonglong(originPointId_))
                                                 .arg(building_.local_frame_id)
                   : frameReference_.valid ? QString("origin=(%1, %2, %3), point ID=%4")
                                                 .arg(frameReference_.latitude_deg, 0, 'f', 8)
                                                 .arg(frameReference_.longitude_deg, 0, 'f', 8)
                                                 .arg(frameReference_.altitude_m, 0, 'f', 2)
                                                 .arg(qulonglong(originPointId_))
                                           : QStringLiteral("origin unavailable");
  // QString validity = validation.valid
  //                        ? QString("%1 closed
  //                        polygon(s)").arg(qulonglong(validation.loops.size())) :
  //                        validation.errors.join("; ");
  QString validity;
  if (validation.valid) {
    validity = QStringLiteral("%1 closed polygon(s)").arg(qulonglong(validation.loops.size()));
  } else if (!validation.errors.isEmpty()) {
    validity = validation.errors.first();
    if (validation.errors.size() > 1) {
      validity += QStringLiteral(" (+%1 detail(s))").arg(validation.errors.size() - 1);
    }
  } else {
    validity = QStringLiteral("Invalid building layout");
  }
  frameInfo_->setText(QString("%1%2 | Source: %3 | %4 | %5 | %6")
                          .arg(frameHeld_ ? "HOLD" : "LOAD")
                          .arg(frameDirty_ ? "*" : "")
                          .arg(source)
                          .arg(dimensions)
                          .arg(origin)
                          .arg(validity));
}

void MainWindow::render() {
  QString error;
  const auto frame = RasterRenderer::buildFrame(currentRegion(), &error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Render Failed", error);
    return;
  }
  if (settings_.power_max_dbfs <= settings_.power_min_dbfs) {
    QMessageBox::warning(this, "Render Failed",
                         "Power maximum must be greater than power minimum.");
    return;
  }
  if (!building_.points.empty()) {
    const auto validation = RasterRenderer::validateBuilding(building_);
    if (!validation.valid) {
      QMessageBox::warning(this, "Invalid Building Layout", validation.errors.join('\n'));
      return;
    }
  }
  const auto bundle =
      RasterRenderer::render(radio_, building_, frame, defaultPower(), settings_.power_min_dbfs,
                             settings_.power_max_dbfs, settings_.render_options);
  QString kind;
  switch (renderType_->currentIndex()) {
    case 0:
      kind = "building";
      break;
    case 1:
      kind = "tx";
      break;
    case 2:
      kind = "radiomap";
      break;
    default:
      kind = /*"sample_count"*/ "sample_site_mask";
  }
  auto metadata = bundle.metadata;
  if (radio_.private_enu || building_.private_enu) {
    metadata.remove("origin_latitude_deg");
    metadata.remove("origin_longitude_deg");
    metadata.remove("origin_altitude_m");
    metadata["coordinate_authority"] = QStringLiteral("PRIVATE_ENU");
    metadata["local_frame_id"] = localFrameId_;
  } else {
    metadata["origin_latitude_deg"] = frameReference_.latitude_deg;
    metadata["origin_longitude_deg"] = frameReference_.longitude_deg;
    metadata["origin_altitude_m"] = frameReference_.altitude_m;
  }
  metadata["origin_point_id"] = QString::number(qulonglong(originPointId_));
  RasterPreviewDialog dialog(bundle, renderType_->currentIndex(), kind, metadata, this);
  dialog.setWindowTitle("Render Preview - " + renderType_->currentText());
  dialog.exec();
}

void MainWindow::rawRecovery() {
  RawRecoveryDialog dialog(fallback(settings_), this);
  dialog.exec();
}

void MainWindow::renderCheck() {
  const auto paths = QFileDialog::getOpenFileNames(this, "Select Rendered Images", {},
                                                   "Images (*.png *.bmp *.jpg)");
  if (paths.isEmpty())
    return;
  QDialog dialog(this);
  dialog.setWindowTitle("Render Alignment Check");
  dialog.resize(900, 720);
  auto* root = new QVBoxLayout(&dialog);
  auto* combo = new QComboBox;
  for (const auto& path : paths)
    combo->addItem(QFileInfo(path).fileName(), path);
  combo->setToolTip("Choose the fully highlighted layer; others remain translucent.");
  auto* scene = new QGraphicsScene(&dialog);
  auto* view = new ZoomGraphicsView(scene);
  view->setDragMode(QGraphicsView::ScrollHandDrag);
  view->setToolTip("Mouse wheel zooms; drag to inspect alignment.");
  root->addWidget(combo);
  root->addWidget(view, 1);
  auto rebuild = [=] {
    scene->clear();
    QSize expected;
    for (int i = 0; i < paths.size(); ++i) {
      QPixmap pixmap(paths[i]);
      if (expected.isEmpty())
        expected = pixmap.size();
      auto* item = scene->addPixmap(pixmap);
      item->setOpacity(i == combo->currentIndex() ? 1.0 : .2);
    }
    scene->setSceneRect(scene->itemsBoundingRect());
  };
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [=](int) {
    rebuild();
  });
  rebuild();
  dialog.exec();
}

void MainWindow::refresh() {
  map_->setSources(&radio_, &building_);
  map_->setRasterRegion(currentRegion());
  const auto tx = std::count_if(radio_.points.begin(), radio_.points.end(), [](const auto& p) {
    return p.role == RadioRole::Tx;
  });
  const auto rx = radio_.points.size() - size_t(tx);
  const auto visibleRadio =
      std::count_if(radio_.points.begin(), radio_.points.end(), [](const auto& point) {
        return point.visible;
      });
  const auto visibleBuilding =
      std::count_if(building_.points.begin(), building_.points.end(), [](const auto& point) {
        return point.visible;
      });
  const QString reference =
      radio_.private_enu ? QString("Private ENU (%1)").arg(radio_.local_frame_id)
      : radio_.reference.valid
          ? (radio_.reference.source == ReferenceSource::File
                 ? QStringLiteral("Original/file WGS84") +
                       (frameHeld_ ? QStringLiteral(" (held project frame)") : QString())
                 : QStringLiteral("Synthetic WGS84 (Settings anchor)"))
          : QStringLiteral("not set");
  if (exportSyntheticAction_) {
    const bool enabled = !radio_.points.empty() && !building_.points.empty() &&
                         radio_.private_enu && building_.private_enu &&
                         !radio_.local_frame_id.trimmed().isEmpty() &&
                         radio_.local_frame_id == building_.local_frame_id;
    exportSyntheticAction_->setEnabled(enabled);
  }
  status_->setText(
      QString("Reference: %1 | RX: %2 | TX: %3 | Visible A: %4/%5 | Building points: %6 "
              "(visible %7) | Edges: %8 | Default: %9 dBFS | Data: in-memory; imported sources "
              "read-only%10%11")
          .arg(reference)
          .arg(rx)
          .arg(tx)
          .arg(qulonglong(visibleRadio))
          .arg(qulonglong(radio_.points.size()))
          .arg(qulonglong(building_.points.size()))
          .arg(qulonglong(visibleBuilding))
          .arg(qulonglong(building_.edges.size()))
          .arg(defaultPower(), 0, 'f', 2)
          .arg((dirtyA_ || dirtyB_ || frameDirty_) ? " | UNSAVED" : "")
          .arg(lastExportStatus_.isEmpty() ? QString()
                                           : QStringLiteral(" | ") + lastExportStatus_));
  updateFrameUi();
}

}  // namespace b210post
