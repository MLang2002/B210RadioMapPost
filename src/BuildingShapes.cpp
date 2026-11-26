#include "BuildingShapes.hpp"

#include <algorithm>
#include <cmath>

namespace b210post {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

QPointF rotateAndTranslate(const QPointF& local, const QPointF& center, double degrees) {
    const double radians = degrees * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {center.x() + cosine * local.x() - sine * local.y(),
            center.y() + sine * local.x() + cosine * local.y()};
}

class RectangleBuildingShape final : public BuildingShape {
  public:
    explicit RectangleBuildingShape(RectangleShapeSnapshot snapshot)
        : snapshot_(std::move(snapshot)) {}

    BuildingShapeKind kind() const override {
        return BuildingShapeKind::Rectangle;
    }

    QPointF center() const override {
        return {snapshot_.center_east_m, snapshot_.center_north_m};
    }

    void setCenter(const QPointF& center) override {
        snapshot_.center_east_m = center.x();
        snapshot_.center_north_m = center.y();
    }

    QPolygonF previewOutline() const override {
        const auto corners = rectangleCorners();
        QPolygonF result;
        for (const auto& corner : corners)
            result << corner;
        return result;
    }

    std::vector<QPointF> discretize() const override {
        const auto corners = rectangleCorners();
        std::vector<QPointF> result;
        for (std::size_t edge = 0; edge < corners.size(); ++edge) {
            const QPointF start = corners[edge];
            const QPointF end = corners[(edge + 1) % corners.size()];
            const int count =
                snapshot_.discretize_edge[edge] ? std::max(2, snapshot_.points_per_edge[edge]) : 2;
            // Exclude the edge endpoint: it is emitted once as the next edge's start vertex.
            for (int index = 0; index < count - 1; ++index) {
                const double t = static_cast<double>(index) / static_cast<double>(count - 1);
                result.push_back(start + t * (end - start));
            }
        }
        return result;
    }

    BuildingShapeSnapshot snapshot() const override {
        return snapshot_;
    }

    std::unique_ptr<BuildingShape> cloneAt(const QPointF& center) const override {
        auto copy = snapshot_;
        copy.center_east_m = center.x();
        copy.center_north_m = center.y();
        return std::make_unique<RectangleBuildingShape>(copy);
    }

  private:
    std::array<QPointF, 4> rectangleCorners() const {
        const double halfWidth = snapshot_.width_m * 0.5;
        const double halfHeight = snapshot_.height_m * 0.5;
        const QPointF centerPoint(snapshot_.center_east_m, snapshot_.center_north_m);
        const std::array<QPointF, 4> local{{{-halfWidth, -halfHeight},
                {halfWidth, -halfHeight},
                {halfWidth, halfHeight},
                {-halfWidth, halfHeight}
            }};
        std::array<QPointF, 4> world;
        for (std::size_t index = 0; index < local.size(); ++index)
            world[index] = rotateAndTranslate(local[index], centerPoint, snapshot_.rotation_deg);
        return world;
    }

    RectangleShapeSnapshot snapshot_;
};

class CircleBuildingShape final : public BuildingShape {
  public:
    explicit CircleBuildingShape(CircleShapeSnapshot snapshot) : snapshot_(std::move(snapshot)) {}

    BuildingShapeKind kind() const override {
        return BuildingShapeKind::Circle;
    }

    QPointF center() const override {
        return {snapshot_.center_east_m, snapshot_.center_north_m};
    }

    void setCenter(const QPointF& center) override {
        snapshot_.center_east_m = center.x();
        snapshot_.center_north_m = center.y();
    }

    QPolygonF previewOutline() const override {
        return polygon(128);
    }

    std::vector<QPointF> discretize() const override {
        const auto points = polygon(std::max(3, snapshot_.circumference_points));
        return {points.cbegin(), points.cend()};
    }

    BuildingShapeSnapshot snapshot() const override {
        return snapshot_;
    }

    std::unique_ptr<BuildingShape> cloneAt(const QPointF& center) const override {
        auto copy = snapshot_;
        copy.center_east_m = center.x();
        copy.center_north_m = center.y();
        return std::make_unique<CircleBuildingShape>(copy);
    }

  private:
    QPolygonF polygon(int count) const {
        QPolygonF result;
        result.reserve(count);
        for (int index = 0; index < count; ++index) {
            const double angle = 2.0 * kPi * static_cast<double>(index) / static_cast<double>(count);
            result << QPointF(snapshot_.center_east_m + snapshot_.radius_m * std::cos(angle),
                              snapshot_.center_north_m + snapshot_.radius_m * std::sin(angle));
        }
        return result;
    }

    CircleShapeSnapshot snapshot_;
};

}  // namespace

std::unique_ptr<BuildingShape> createBuildingShape(const BuildingShapeSnapshot& snapshot) {
    if (const auto* rectangle = std::get_if<RectangleShapeSnapshot>(&snapshot))
        return std::make_unique<RectangleBuildingShape>(*rectangle);
    return std::make_unique<CircleBuildingShape>(std::get<CircleShapeSnapshot>(snapshot));
}

BuildingShapeKind shapeKind(const BuildingShapeSnapshot& snapshot) {
    return std::holds_alternative<RectangleShapeSnapshot>(snapshot) ? BuildingShapeKind::Rectangle
           : BuildingShapeKind::Circle;
}

BuildingShapeSnapshot shapeMovedTo(const BuildingShapeSnapshot& snapshot, const QPointF& center) {
    auto shape = createBuildingShape(snapshot);
    shape->setCenter(center);
    return shape->snapshot();
}

}  // namespace b210post
