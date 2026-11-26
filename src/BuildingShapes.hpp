#ifndef __B210_BUILDING_SHAPES_HPP
#define __B210_BUILDING_SHAPES_HPP

#include <QPointF>
#include <QPolygonF>
#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace b210post {

enum class BuildingShapeKind { Rectangle, Circle };
enum class RulerOrientation { Horizontal, Vertical };
enum class BuildingDesignPickMode {
    None,
    PlaceShape,
    CopyShape,
    PlaceHorizontalRuler,
    PlaceVerticalRuler,
    MeasureRulers
};

struct RectangleShapeSnapshot {
    double center_east_m = 0.0;
    double center_north_m = 0.0;
    double width_m = 10.0;
    double height_m = 10.0;
    double rotation_deg = 0.0;
    double linear_step = 0.1;
    double angle_step = 1.0;
    std::array<bool, 4> discretize_edge{{false, false, false, false}};
    // Counts include both endpoints. A disabled edge contributes only its start vertex.
    std::array<int, 4> points_per_edge{{2, 2, 2, 2}};
};

struct CircleShapeSnapshot {
    double center_east_m = 0.0;
    double center_north_m = 0.0;
    double radius_m = 5.0;
    int circumference_points = 32;
    double linear_step = 0.1;
};

using BuildingShapeSnapshot = std::variant<RectangleShapeSnapshot, CircleShapeSnapshot>;

class BuildingShape {
  public:
    virtual ~BuildingShape() = default;
    virtual BuildingShapeKind kind() const = 0;
    virtual QPointF center() const = 0;
    virtual void setCenter(const QPointF& center) = 0;
    virtual QPolygonF previewOutline() const = 0;
    virtual std::vector<QPointF> discretize() const = 0;
    virtual BuildingShapeSnapshot snapshot() const = 0;
    virtual std::unique_ptr<BuildingShape> cloneAt(const QPointF& center) const = 0;
};

std::unique_ptr<BuildingShape> createBuildingShape(const BuildingShapeSnapshot& snapshot);
BuildingShapeKind shapeKind(const BuildingShapeSnapshot& snapshot);
BuildingShapeSnapshot shapeMovedTo(const BuildingShapeSnapshot& snapshot, const QPointF& center);

struct BuildingShapeOverlay {
    std::uint64_t id = 0;
    QPolygonF outline;
    std::vector<QPointF> discrete_points;
    bool selected = false;
};

struct BuildingRulerOverlay {
    std::uint64_t id = 0;
    RulerOrientation orientation = RulerOrientation::Horizontal;
    double position_m = 0.0;
    bool visible = true;
    bool measurement_selected = false;
};

struct BuildingDesignOverlay {
    std::vector<BuildingShapeOverlay> shapes;
    std::vector<BuildingRulerOverlay> rulers;
};

}  // namespace b210post

#endif // __B210_BUILDING_SHAPES_HPP
