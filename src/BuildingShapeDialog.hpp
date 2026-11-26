#ifndef __B210_BUILDING_SHAPE_DIALOG_HPP
#define __B210_BUILDING_SHAPE_DIALOG_HPP

#include <QDialog>
#include <functional>
#include <optional>
#include <vector>

#include "BuildingShapes.hpp"

class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QTableWidget;

namespace b210post {

class ShapeParameterWidget : public QWidget {
    Q_OBJECT
  public:
    explicit ShapeParameterWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual BuildingShapeKind shapeKind() const = 0;
    virtual BuildingShapeSnapshot snapshot() const = 0;
    virtual void restore(const BuildingShapeSnapshot& snapshot) = 0;

  signals:
    void parametersChanged();
};

class BuildingShapeDialog : public QDialog {
    Q_OBJECT
  public:
    struct Callbacks {
        std::function<void(const BuildingDesignOverlay&)> update_overlay;
        std::function<void(BuildingDesignPickMode)> set_pick_mode;
        std::function<bool(const std::vector<BuildingShapeSnapshot>&, QString&)> commit;
        std::function<void()> clear_overlay;
    };

    explicit BuildingShapeDialog(Callbacks callbacks, QWidget* parent = nullptr);
    void setSourceNotice(const QString& notice);
    void acceptPickedPosition(const QPointF& position);
    void selectShape(std::uint64_t id);
    void selectRuler(std::uint64_t id);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    struct ShapeRecord {
        std::uint64_t id = 0;
        BuildingShapeSnapshot snapshot;
    };
    struct RulerRecord {
        std::uint64_t id = 0;
        RulerOrientation orientation = RulerOrientation::Horizontal;
        double position_m = 0.0;
        double step = 0.1;
        bool visible = true;
    };

    ShapeParameterWidget* parameterWidget(BuildingShapeKind kind) const;
    ShapeRecord* findShape(std::uint64_t id);
    RulerRecord* findRuler(std::uint64_t id);
    void beginPlaceShape();
    void beginCopyShape();
    void removeSelectedShape();
    void onParametersChanged();
    void rebuildShapeList();
    void rebuildRulerTable();
    void publishOverlay();
    void addRuler(RulerOrientation orientation, const QPointF& position);
    void beginMeasurement();
    void updateMeasurement(const std::vector<std::uint64_t>& ids);
    std::vector<std::uint64_t> selectedRulerIds() const;
    void setPickMode(BuildingDesignPickMode mode);

    Callbacks callbacks_;
    std::vector<ShapeRecord> shapes_;
    std::vector<RulerRecord> rulers_;
    std::vector<std::uint64_t> measurementIds_;
    std::optional<BuildingShapeSnapshot> pendingShape_;
    std::uint64_t nextShapeId_ = 1;
    std::uint64_t nextRulerId_ = 1;
    std::uint64_t selectedShapeId_ = 0;
    BuildingDesignPickMode pickMode_ = BuildingDesignPickMode::None;
    bool loadingParameters_ = false;
    bool rebuildingRulers_ = false;

    QComboBox* newShapeType_ = nullptr;
    QListWidget* shapeList_ = nullptr;
    QStackedWidget* parameterStack_ = nullptr;
    ShapeParameterWidget* rectangleParameters_ = nullptr;
    ShapeParameterWidget* circleParameters_ = nullptr;
    QTableWidget* rulerTable_ = nullptr;
    QLabel* measureResult_ = nullptr;
    QLabel* sourceNotice_ = nullptr;
    QLabel* instruction_ = nullptr;
};

}  // namespace b210post

#endif // __B210_BUILDING_SHAPE_DIALOG_HPP
