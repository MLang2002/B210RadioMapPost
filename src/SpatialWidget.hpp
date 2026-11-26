#ifndef __B210_SPATIAL_WIDGET_HPP
#define __B210_SPATIAL_WIDGET_HPP

#include <QList>
#include <QWidget>
#include <optional>
#include <set>

#include "BuildingShapes.hpp"
#include "Interpolation.hpp"
#include "Types.hpp"

class QPainter;

namespace b210post {

class SpatialWidget : public QWidget {
    Q_OBJECT
  public:
    enum class EditLayer { Radio, Building };
    explicit SpatialWidget(QWidget* parent = nullptr);
    void setSources(const RadioDataset* radio, const BuildingDataset* building);
    void setEditLayer(EditLayer layer);
    void setSettings(const AppSettings& settings);
    void setRasterRegion(const RasterRegion& region);
    void setShowRasterRegion(bool show);
    void setPlacement(bool on);
    void beginInterpolationRegionSelection();
    void cancelInterpolationRegionSelection();
    void setInterpolationPreview(const QRectF& region,
                                 const std::vector<InterpolatedSample>& samples);
    void clearInterpolationPreview();
    void setSelectedRadios(const QList<qulonglong>& ids);
    void setSelectedBuildings(const QList<qulonglong>& ids);
    void setSelectedEdge(std::optional<int> index);
    void setBuildingDesignOverlay(const BuildingDesignOverlay& overlay);
    void clearBuildingDesignOverlay();
    void setBuildingDesignPickMode(BuildingDesignPickMode mode);
    void resetView();

  signals:
    void radioSelectionChanged(QList<qulonglong> ids);
    void buildingSelectionChanged(QList<qulonglong> ids);
    void edgeSelected(int index);
    void placeRequested(double east, double north);
    void interpolationRegionSelected(QRectF region);
    void buildingDesignPositionPicked(QPointF position);
    void buildingDesignShapeSelected(qulonglong id);
    void buildingDesignRulerSelected(qulonglong id);

  protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

  private:
    QRectF plotRect() const;
    QPointF worldToScreen(double east, double north) const;
    QPointF screenToWorld(QPointF point) const;
    void drawGrid(QPainter& painter) const;
    void fit();
    QList<qulonglong> selectedRadioList() const;
    QList<qulonglong> selectedBuildingList() const;

    const RadioDataset* radio_ = nullptr;
    const BuildingDataset* building_ = nullptr;
    EditLayer layer_ = EditLayer::Radio;
    AppSettings settings_;
    RasterRegion rasterRegion_;
    QPointF center_;
    double baseScale_ = 5;
    double zoom_ = 1;
    QPointF pan_;
    bool panning_ = false;
    bool placement_ = false;
    bool showRasterRegion_ = false;
    bool selectingInterpolationRegion_ = false;
    bool draggingInterpolationRegion_ = false;
    QPoint last_;
    QPointF interpolationDragStart_;
    QRectF interpolationRegion_;
    std::vector<InterpolatedSample> interpolationPreview_;
    std::set<std::uint64_t> selectedRadios_;
    std::set<std::uint64_t> selectedBuildings_;
    std::optional<int> selectedEdge_;
    BuildingDesignOverlay buildingDesignOverlay_;
    BuildingDesignPickMode buildingDesignPickMode_ = BuildingDesignPickMode::None;
};

}  // namespace b210post

#endif // __B210_SPATIAL_WIDGET_HPP
