#ifndef __B210_MAIN_WINDOW_HPP
#define __B210_MAIN_WINDOW_HPP

#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <memory>
#include <vector>

#include "BuildingShapes.hpp"
#include "CsvCodec.hpp"
#include "Interpolation.hpp"
#include "Models.hpp"
#include "Types.hpp"

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableView;

namespace b210post {

class SpatialWidget;
class LocalFillDialog;
class BuildingShapeDialog;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow();

 protected:
  void closeEvent(QCloseEvent*) override;

 private slots:
  void importData();
  void saveA();
  void saveB();
  void saveFrame();
  void loadOrReleaseFrame();
  void editSettings();
  void setMode();
  void togglePlace();
  void place(double east, double north);
  void deleteSelected();
  void deleteEdge();
  void toggleRadioRole();
  void addEdge();
  void render();
  void rawRecovery();
  void renderCheck();
  void rasterSettingsChanged();
  void createPrivateCopy();
  void exportSyntheticWgs84Copies();
  void openLocalFill();
  void openBuildingShapeEditor();

 private:
  void loadSettings();
  void persistSettings();
  bool confirmDiscard();
  bool loadBuildingFile(const QString& path, bool fromImport);
  bool resolveBuildingReference(const BuildingSource& source, EnuReference& reference,
                                std::uint64_t& originPointId, QString& error) const;
  void applyProjectReference(const EnuReference& reference);
  void applyRadioOnlyReference();
  void applyFrameSourceSettings(const BuildingSource& source);
  void updateFrameValidity();
  void updateFrameUi();
  void applySyntheticReference(const EnuReference& reference);
  QString normalizedPath(const QString& path) const;
  bool protectImportedSource(const QString& path);
  bool canWritePath(const QString& path);
  bool coordinateModesCompatible(const RadioDataset& radio, const BuildingDataset& building,
                                 QString& error) const;
  RasterRegion currentRegion() const;
  double defaultPower() const;
  void refresh();
  void beginInterpolationRegionSelection();
  void previewInterpolation(const QString& methodId, const QJsonObject& parameters,
                            const QRectF& region);
  void commitInterpolation();
  void clearInterpolationPreview();
  void invalidateInterpolationPreview(const QString& reason);
  const InterpolationMethod* interpolationMethod(const QString& id) const;
  bool commitBuildingShapes(const std::vector<BuildingShapeSnapshot>& snapshots, QString& error);

  RadioDataset radio_;
  BuildingDataset building_;
  RadioTableModel radioModel_;
  BuildingTableModel buildingModel_;
  AppSettings settings_;
  EnuReference frameReference_;
  std::uint64_t originPointId_ = 0;
  QString pathA_;
  QString pathB_;
  QString framePath_;
  QSet<QString> protectedSourcePaths_;
  QString localFrameId_;
  QString lastExportStatus_;
  bool dirtyA_ = false;
  bool dirtyB_ = false;
  bool frameHeld_ = false;
  bool frameDirty_ = false;
  bool frameFromRasterFile_ = false;
  bool placing_ = false;
  bool updatingRasterUi_ = false;
  int selectedEdge_ = -1;
  std::vector<std::unique_ptr<InterpolationMethod>> interpolationMethods_;
  InterpolationResult interpolationPreview_;
  QString interpolationPreviewMethodId_;
  QJsonObject interpolationPreviewParameters_;
  QRectF interpolationRegion_;
  QPointer<LocalFillDialog> localFillDialog_;
  QPointer<BuildingShapeDialog> buildingShapeDialog_;

  SpatialWidget* map_;
  QTableView* radioTable_;
  QTableView* buildingTable_;
  QComboBox* mode_;
  QComboBox* renderType_;
  QComboBox* colorMode_;
  QComboBox* supersampling_;
  QComboBox* resamplingFilter_;
  QPushButton* placeButton_;
  QPushButton* frameButton_;
  QPushButton* saveFrameButton_;
  QPushButton* showRegionButton_;
  QPushButton* localFillButton_;
  QPushButton* buildingShapeButton_;
  QPushButton* toggleRoleButton_;
  QPushButton* connectPointsButton_;
  QPushButton* deleteEdgeButton_;
  QAction* exportSyntheticAction_ = nullptr;
  QCheckBox* autoPower_;
  QCheckBox* colorbar_;
  QCheckBox* smoothExport_;
  QDoubleSpinBox* left_;
  QDoubleSpinBox* right_;
  QDoubleSpinBox* bottom_;
  QDoubleSpinBox* top_;
  QDoubleSpinBox* mpp_;
  QDoubleSpinBox* rotation_;
  QDoubleSpinBox* defaultPower_;
  QDoubleSpinBox* powerMin_;
  QDoubleSpinBox* powerMax_;
  QLabel* frameInfo_;
  QLabel* status_;
};

}  // namespace b210post

#endif  // __B210_MAIN_WINDOW_HPP
