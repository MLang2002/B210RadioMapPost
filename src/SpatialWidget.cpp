#include "SpatialWidget.hpp"

#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace b210post {
namespace {
double niceGridStep(double metersPerTarget) {
    if (!(metersPerTarget > 0.0))
        return 1.0;
    const double decade = std::pow(10.0, std::floor(std::log10(metersPerTarget)));
    const double scaled = metersPerTarget / decade;
    if (scaled > 5.0)
        return 10.0 * decade;
    if (scaled > 2.0)
        return 5.0 * decade;
    if (scaled > 1.0)
        return 2.0 * decade;
    return decade;
}

double segmentDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    QLineF ab(a, b);
    if (ab.length() < 1e-9)
        return QLineF(p, a).length();
    const QPointF v = b - a, w = p - a;
    const double t = std::clamp(QPointF::dotProduct(w, v) / QPointF::dotProduct(v, v), 0.0, 1.0);
    return QLineF(p, a + t * v).length();
}

QPointF rotateLocalToWorld(const QPointF& local, double rotationDeg) {
    const double radians = rotationDeg * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {cosine * local.x() - sine * local.y(), sine * local.x() + cosine * local.y()};
}
}  // namespace

SpatialWidget::SpatialWidget(QWidget* p) : QWidget(p) {
    setMinimumSize(520, 400);
    setMouseTracking(true);
}

void SpatialWidget::setSources(const RadioDataset* r, const BuildingDataset* b) {
    const bool first = !radio_ && !building_;
    radio_ = r;
    building_ = b;
    if (first)
        fit();
    update();
}

void SpatialWidget::setEditLayer(EditLayer l) {
    layer_ = l;
    placement_ = false;
    update();
}

void SpatialWidget::setSettings(const AppSettings& s) {
    settings_ = s;
    update();
}

void SpatialWidget::setRasterRegion(const RasterRegion& r) {
    rasterRegion_ = r;
    update();
}

void SpatialWidget::setShowRasterRegion(bool show) {
    showRasterRegion_ = show;
    if (show)
        fit();
    update();
}

void SpatialWidget::setPlacement(bool on) {
    placement_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void SpatialWidget::beginInterpolationRegionSelection() {
    placement_ = false;
    selectingInterpolationRegion_ = true;
    draggingInterpolationRegion_ = false;
    setCursor(Qt::CrossCursor);
    update();
}

void SpatialWidget::cancelInterpolationRegionSelection() {
    selectingInterpolationRegion_ = false;
    draggingInterpolationRegion_ = false;
    setCursor(placement_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void SpatialWidget::setInterpolationPreview(const QRectF& region,
        const std::vector<InterpolatedSample>& samples) {
    interpolationRegion_ = region.normalized();
    interpolationPreview_ = samples;
    update();
}

void SpatialWidget::clearInterpolationPreview() {
    interpolationPreview_.clear();
    update();
}

void SpatialWidget::setSelectedRadios(const QList<qulonglong>& ids) {
    selectedRadios_.clear();
    for (const auto id : ids) {
        selectedRadios_.insert(static_cast<std::uint64_t>(id));
    }
    update();
}

void SpatialWidget::setSelectedBuildings(const QList<qulonglong>& ids) {
    selectedBuildings_.clear();
    for (const auto id : ids)
        selectedBuildings_.insert(std::uint64_t(id));
    update();
}

void SpatialWidget::setSelectedEdge(std::optional<int> index) {
    selectedEdge_ = index;
    update();
}

void SpatialWidget::setBuildingDesignOverlay(const BuildingDesignOverlay& overlay) {
    buildingDesignOverlay_ = overlay;
    update();
}

void SpatialWidget::clearBuildingDesignOverlay() {
    buildingDesignOverlay_ = {};
    buildingDesignPickMode_ = BuildingDesignPickMode::None;
    setCursor(placement_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void SpatialWidget::setBuildingDesignPickMode(BuildingDesignPickMode mode) {
    buildingDesignPickMode_ = mode;
    if (mode != BuildingDesignPickMode::None) {
        placement_ = false;
        selectingInterpolationRegion_ = false;
        draggingInterpolationRegion_ = false;
    }
    setCursor(mode == BuildingDesignPickMode::None ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

QList<qulonglong> SpatialWidget::selectedBuildingList() const {
    QList<qulonglong> ids;
    for (const auto id : selectedBuildings_)
        ids << qulonglong(id);
    return ids;
}

QList<qulonglong> SpatialWidget::selectedRadioList() const {
    QList<qulonglong> ids;
    for (const auto id : selectedRadios_) {
        ids << qulonglong(id);
    }
    return ids;
}

QRectF SpatialWidget::plotRect() const {
    return rect().adjusted(48, 24, -92, -42);
}

QPointF SpatialWidget::worldToScreen(double e, double n) const {
    const auto p = plotRect();
    const double s = baseScale_ * zoom_;
    return {p.center().x() + (e - center_.x()) * s + pan_.x(),
            p.center().y() - (n - center_.y()) * s + pan_.y()};
}

QPointF SpatialWidget::screenToWorld(QPointF p) const {
    const auto q = plotRect();
    const double s = std::max(1e-9, baseScale_ * zoom_);
    return {center_.x() + (p.x() - q.center().x() - pan_.x()) / s,
            center_.y() - (p.y() - q.center().y() - pan_.y()) / s};
}

void SpatialWidget::drawGrid(QPainter& painter) const {
    const QRectF plot = plotRect();
    const QPointF lowerLeft = screenToWorld(plot.bottomLeft());
    const QPointF upperRight = screenToWorld(plot.topRight());
    const double minEast = std::min(lowerLeft.x(), upperRight.x());
    const double maxEast = std::max(lowerLeft.x(), upperRight.x());
    const double minNorth = std::min(lowerLeft.y(), upperRight.y());
    const double maxNorth = std::max(lowerLeft.y(), upperRight.y());
    const double scale = std::max(1e-12, baseScale_ * zoom_);
    const double step = niceGridStep(85.0 / scale);
    const double firstEast = std::floor(minEast / step) * step;
    const double firstNorth = std::floor(minNorth / step) * step;

    painter.save();
    painter.setClipRect(plot);
    painter.setPen(QPen(palette().midlight().color(), 1));
    for (double east = firstEast; east <= maxEast + 0.5 * step; east += step) {
        const double x = worldToScreen(east, 0.0).x();
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (double north = firstNorth; north <= maxNorth + 0.5 * step; north += step) {
        const double y = worldToScreen(0.0, north).y();
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.restore();

    painter.setPen(palette().mid().color());
    painter.drawRect(plot);
    painter.setPen(palette().text().color());
    for (double east = firstEast; east <= maxEast + 0.5 * step; east += step) {
        const double x = worldToScreen(east, 0.0).x();
        if (x >= plot.left() && x <= plot.right())
            painter.drawText(QPointF(x + 2.0, plot.bottom() + 16.0),
                             QString::number(east, 'f', step < 1.0 ? 1 : 0));
    }
    for (double north = firstNorth; north <= maxNorth + 0.5 * step; north += step) {
        const double y = worldToScreen(0.0, north).y();
        if (y >= plot.top() && y <= plot.bottom())
            painter.drawText(QPointF(4.0, y + 4.0), QString::number(north, 'f', step < 1.0 ? 1 : 0));
    }
}

void SpatialWidget::fit() {
    double minE = std::numeric_limits<double>::infinity(), minN = minE, maxE = -minE, maxN = -minE;
    auto add = [&](double e, double n) {
        if (!std::isfinite(e) || !std::isfinite(n))
            return;
        minE = std::min(minE, e);
        maxE = std::max(maxE, e);
        minN = std::min(minN, n);
        maxN = std::max(maxN, n);
    };
    if (radio_) {
        for (const auto& point : radio_->points) {
            if (point.visible) {
                add(point.east_m, point.north_m);
            }
        }
    }
    if (building_) {
        for (const auto& p : building_->points)
            add(p.east_m, p.north_m);
    }
    if (showRasterRegion_) {
        const QPointF corners[] = {{-rasterRegion_.left_m, -rasterRegion_.bottom_m},
            {rasterRegion_.right_m, -rasterRegion_.bottom_m},
            {rasterRegion_.right_m, rasterRegion_.top_m},
            {-rasterRegion_.left_m, rasterRegion_.top_m}
        };
        for (const auto& corner : corners) {
            const auto world = rotateLocalToWorld(corner, rasterRegion_.rotation_deg);
            add(world.x(), world.y());
        }
    }
    if (!std::isfinite(minE)) {
        center_ = {};
        baseScale_ = 5;
    } else {
        center_ = {(minE + maxE) / 2, (minN + maxN) / 2};
        const auto pr = plotRect();
        baseScale_ = 0.86 * std::min(pr.width() / std::max(1.0, maxE - minE),
                                     pr.height() / std::max(1.0, maxN - minN));
    }
    zoom_ = 1;
    pan_ = {};
}

void SpatialWidget::resetView() {
    fit();
    update();
}

void SpatialWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    p.setRenderHint(QPainter::Antialiasing);
    const auto pr = plotRect();
    drawGrid(p);
    double minAlt = std::numeric_limits<double>::infinity(), maxAlt = -minAlt;
    if (building_)
        for (const auto& q : building_->points)
            if (std::isfinite(q.altitude_m)) {
                minAlt = std::min(minAlt, q.altitude_m);
                maxAlt = std::max(maxAlt, q.altitude_m);
            }
    if (!std::isfinite(minAlt)) {
        minAlt = 0;
        maxAlt = 1;
    }
    if (!(maxAlt > minAlt))
        maxAlt = minAlt + 1;
    double minPower = std::numeric_limits<double>::infinity();
    double maxPower = -std::numeric_limits<double>::infinity();
    if (radio_) {
        for (const auto& point : radio_->points) {
            if (!point.visible || point.role != RadioRole::Rx || !point.valid ||
                    !std::isfinite(point.power_dbfs))
                continue;
            minPower = std::min(minPower, point.power_dbfs);
            maxPower = std::max(maxPower, point.power_dbfs);
        }
    }
    if (!std::isfinite(minPower)) {
        minPower = settings_.power_min_dbfs;
        maxPower = settings_.power_max_dbfs;
    } else if (!(maxPower > minPower)) {
        minPower -= 0.5;
        maxPower += 0.5;
    }
    p.setClipRect(pr);
    const double radioOpacity = layer_ == EditLayer::Radio ? 1 : settings_.inactive_opacity,
                 buildingOpacity = layer_ == EditLayer::Building ? 1 : settings_.inactive_opacity;
    if (building_) {
        p.save();
        p.setOpacity(buildingOpacity);
        std::unordered_map<std::uint64_t, const SurveyPoint*> points;
        for (const auto& q : building_->points)
            points[q.id] = &q;
        for (size_t i = 0; i < building_->edges.size(); ++i) {
            const auto edge = building_->edges[i];
            if (points.count(edge.a) && points.count(edge.b)) {
                const bool selected = selectedEdge_ && *selectedEdge_ == int(i);
                p.setPen(QPen(selected ? palette().highlight().color() : QColor(70, 70, 70),
                              selected ? 3.0 : 1.5));
                p.drawLine(worldToScreen(points[edge.a]->east_m, points[edge.a]->north_m),
                           worldToScreen(points[edge.b]->east_m, points[edge.b]->north_m));
            }
        }
        for (const auto& q : building_->points) {
            if (!q.visible) {
                continue;
            }
            const auto screen = worldToScreen(q.east_m, q.north_m);
            const bool selected = selectedBuildings_.count(q.id) > 0;
            const double t = std::isfinite(q.altitude_m)
                             ? std::clamp((q.altitude_m - minAlt) / (maxAlt - minAlt), 0.0, 1.0)
                             : 0.5;
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? palette().highlight().color() : QColor::fromHsvF((1 - t) * .66, 1, 1));
            const double size = selected ? settings_.point_size_px : settings_.point_size_px * .55;
            p.drawEllipse(screen, size, size);
        }
        p.restore();
    }
    if (!interpolationRegion_.isEmpty()) {
        p.save();
        p.setPen(QPen(QColor(25, 150, 255), 2, Qt::DashLine));
        p.setBrush(QColor(25, 150, 255, 28));
        const QPointF first = worldToScreen(interpolationRegion_.left(), interpolationRegion_.top());
        const QPointF second =
            worldToScreen(interpolationRegion_.right(), interpolationRegion_.bottom());
        p.drawRect(QRectF(first, second).normalized());
        for (const auto& sample : interpolationPreview_) {
            const double normalized =
                std::clamp((sample.power_dbfs - settings_.power_min_dbfs) /
                           std::max(1e-9, settings_.power_max_dbfs - settings_.power_min_dbfs),
                           0.0, 1.0);
            p.setPen(QPen(Qt::white, 0.8));
            p.setBrush(QColor::fromHsvF((1.0 - normalized) * 0.66, 1.0, 1.0));
            p.drawRect(QRectF(worldToScreen(sample.position.x(), sample.position.y()) - QPointF(2.5, 2.5),
                              QSizeF(5.0, 5.0)));
        }
        p.restore();
    }
    if (radio_) {
        p.save();
        p.setOpacity(radioOpacity);
        const double low = minPower, high = maxPower;
        for (const auto& q : radio_->points) {
            if (!q.visible || !std::isfinite(q.east_m) || !std::isfinite(q.north_m))
                continue;
            const auto screen = worldToScreen(q.east_m, q.north_m);
            const bool selected = selectedRadios_.count(q.id) > 0;
            if (q.role == RadioRole::Tx) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(selected ? palette().highlight().color() : Qt::black, selected ? 3 : 2));
                p.drawLine(screen + QPointF(-7, 0), screen + QPointF(7, 0));
                p.drawLine(screen + QPointF(0, -7), screen + QPointF(0, 7));
                p.drawText(screen + QPointF(8, -6), QString("TX%1").arg(qulonglong(q.id)));
            } else {
                if (!q.valid || !std::isfinite(q.power_dbfs))
                    continue;
                const double t = std::clamp((q.power_dbfs - low) / std::max(1e-9, high - low), 0.0, 1.0);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor::fromHsvF((1 - t) * .66, 1, 1));
                p.drawEllipse(screen, settings_.point_size_px * .5, settings_.point_size_px * .5);
                if (selected) {
                    p.setPen(QPen(palette().highlight().color(), 2));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(screen, settings_.point_size_px + 3, settings_.point_size_px + 3);
                }
            }
        }
        p.restore();
    }
    if (!buildingDesignOverlay_.shapes.empty() || !buildingDesignOverlay_.rulers.empty()) {
        p.save();
        p.setClipRect(pr);
        for (const auto& shape : buildingDesignOverlay_.shapes) {
            QPolygonF screenOutline;
            for (const auto& point : shape.outline)
                screenOutline << worldToScreen(point.x(), point.y());
            p.setPen(QPen(shape.selected ? QColor(0, 125, 255) : QColor(0, 175, 120),
                          shape.selected ? 3.0 : 2.0));
            p.setBrush(shape.selected ? QColor(0, 125, 255, 35) : QColor(0, 175, 120, 22));
            p.drawPolygon(screenOutline);
            p.setPen(Qt::NoPen);
            p.setBrush(shape.selected ? QColor(0, 90, 220) : QColor(0, 135, 90));
            for (const auto& point : shape.discrete_points)
                p.drawEllipse(worldToScreen(point.x(), point.y()), 2.5, 2.5);
        }
        for (const auto& ruler : buildingDesignOverlay_.rulers) {
            if (!ruler.visible)
                continue;
            p.setPen(QPen(ruler.measurement_selected ? QColor(220, 70, 30) : QColor(145, 60, 190),
                          ruler.measurement_selected ? 3.0 : 1.5, Qt::DashLine));
            if (ruler.orientation == RulerOrientation::Horizontal) {
                const double y = worldToScreen(0.0, ruler.position_m).y();
                p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
            } else {
                const double x = worldToScreen(ruler.position_m, 0.0).x();
                p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
            }
        }
        p.restore();
    }
    if (showRasterRegion_) {
        p.save();
        const QPointF localCorners[] = {{-rasterRegion_.left_m, -rasterRegion_.bottom_m},
            {rasterRegion_.right_m, -rasterRegion_.bottom_m},
            {rasterRegion_.right_m, rasterRegion_.top_m},
            {-rasterRegion_.left_m, rasterRegion_.top_m}
        };
        QPolygonF region;
        for (const auto& corner : localCorners) {
            const auto world = rotateLocalToWorld(corner, rasterRegion_.rotation_deg);
            region << worldToScreen(world.x(), world.y());
        }
        QPainterPath shade;
        shade.setFillRule(Qt::OddEvenFill);
        shade.addRect(pr);
        shade.addPolygon(region);
        p.fillPath(shade, QColor(30, 30, 30, 85));
        p.setPen(QPen(QColor(255, 140, 0), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(region);
        const auto origin = worldToScreen(0, 0);
        p.setPen(QPen(QColor(255, 80, 0), 1.5, Qt::DashLine));
        const auto leftWorld =
            rotateLocalToWorld({-rasterRegion_.left_m, 0.0}, rasterRegion_.rotation_deg);
        const auto rightWorld =
            rotateLocalToWorld({rasterRegion_.right_m, 0.0}, rasterRegion_.rotation_deg);
        const auto bottomWorld =
            rotateLocalToWorld({0.0, -rasterRegion_.bottom_m}, rasterRegion_.rotation_deg);
        const auto topWorld =
            rotateLocalToWorld({0.0, rasterRegion_.top_m}, rasterRegion_.rotation_deg);
        p.drawLine(worldToScreen(leftWorld.x(), leftWorld.y()),
                   worldToScreen(rightWorld.x(), rightWorld.y()));
        p.drawLine(worldToScreen(bottomWorld.x(), bottomWorld.y()),
                   worldToScreen(topWorld.x(), topWorld.y()));
        p.setPen(QPen(QColor(255, 40, 0), 2));
        p.setBrush(Qt::white);
        p.drawEllipse(origin, 5, 5);
        p.restore();
    }
    p.setClipping(false);
    p.setPen(palette().text().color());
    p.drawText(8, 18, QStringLiteral("Local ENU [m] — East right, North up"));
    const QRectF bar(pr.right() + 24, pr.top() + 28, 15, std::max(80.0, pr.height() - 56));
    QLinearGradient gradient(bar.bottomLeft(), bar.topLeft());
    gradient.setColorAt(0, QColor::fromHsvF(.66, 1, 1));
    gradient.setColorAt(1, QColor::fromHsvF(0, 1, 1));
    p.fillRect(bar, gradient);
    p.drawRect(bar);
    const bool power = layer_ == EditLayer::Radio;
    const double low = power ? minPower : minAlt, high = power ? maxPower : maxAlt;
    p.drawText(QPointF(bar.left() - 8, bar.top() - 8), power ? "RX Power [dBFS]" : "Altitude [m]");
    p.drawText(QPointF(bar.right() + 4, bar.top() + 5), QString::number(high, 'f', 1));
    p.drawText(QPointF(bar.right() + 4, bar.bottom()), QString::number(low, 'f', 1));
}

void SpatialWidget::wheelEvent(QWheelEvent* e) {
    zoom_ = std::clamp(zoom_ * (e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), .1, 1000.0);
    update();
}

void SpatialWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        last_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton || !plotRect().contains(e->localPos()))
        return;
    if (selectingInterpolationRegion_) {
        draggingInterpolationRegion_ = true;
        interpolationDragStart_ = screenToWorld(e->localPos());
        interpolationRegion_ = QRectF(interpolationDragStart_, interpolationDragStart_);
        interpolationPreview_.clear();
        update();
        return;
    }
    if (buildingDesignPickMode_ != BuildingDesignPickMode::None) {
        if (buildingDesignPickMode_ == BuildingDesignPickMode::MeasureRulers) {
            double bestDistance = 8.0;
            std::uint64_t bestId = 0;
            for (const auto& ruler : buildingDesignOverlay_.rulers) {
                if (!ruler.visible)
                    continue;
                const double distance =
                    ruler.orientation == RulerOrientation::Horizontal
                    ? std::abs(e->localPos().y() - worldToScreen(0.0, ruler.position_m).y())
                    : std::abs(e->localPos().x() - worldToScreen(ruler.position_m, 0.0).x());
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestId = ruler.id;
                }
            }
            if (bestId)
                emit buildingDesignRulerSelected(qulonglong(bestId));
            return;
        }
        emit buildingDesignPositionPicked(screenToWorld(e->localPos()));
        return;
    }
    if (placement_) {
        const auto world = screenToWorld(e->localPos());
        emit placeRequested(world.x(), world.y());
        return;
    }
    if (!buildingDesignOverlay_.shapes.empty()) {
        std::uint64_t shapeId = 0;
        double bestEdgeDistance = 9.0;
        for (auto shape = buildingDesignOverlay_.shapes.rbegin();
                shape != buildingDesignOverlay_.shapes.rend(); ++shape) {
            QPolygonF screenOutline;
            for (const auto& point : shape->outline)
                screenOutline << worldToScreen(point.x(), point.y());
            if (screenOutline.containsPoint(e->localPos(), Qt::OddEvenFill)) {
                shapeId = shape->id;
                break;
            }
            for (int index = 0; index < screenOutline.size(); ++index) {
                const double distance = segmentDistance(e->localPos(), screenOutline[index],
                                                        screenOutline[(index + 1) % screenOutline.size()]);
                if (distance < bestEdgeDistance) {
                    bestEdgeDistance = distance;
                    shapeId = shape->id;
                }
            }
            if (shapeId)
                break;
        }
        if (shapeId) {
            emit buildingDesignShapeSelected(qulonglong(shapeId));
            return;
        }
    }
    double best = 12;
    std::uint64_t id = 0;
    if (layer_ == EditLayer::Radio && radio_) {
        for (const auto& q : radio_->points) {
            if (!q.visible) {
                continue;
            }
            const double distance = QLineF(e->localPos(), worldToScreen(q.east_m, q.north_m)).length();
            if (distance < best) {
                best = distance;
                id = q.id;
            }
        }
        if (id) {
            selectedEdge_.reset();
            emit edgeSelected(-1);
            const bool control = e->modifiers() & Qt::ControlModifier;
            if (!control) {
                selectedRadios_.clear();
            }
            if (control && selectedRadios_.count(id)) {
                selectedRadios_.erase(id);
            } else {
                selectedRadios_.insert(id);
            }
            emit radioSelectionChanged(selectedRadioList());
            update();
            return;
        }
        if (!(e->modifiers() & Qt::ControlModifier) && !selectedRadios_.empty()) {
            selectedRadios_.clear();
            emit radioSelectionChanged(QList<qulonglong> {});
            update();
        }
    }
    if (layer_ == EditLayer::Building && building_) {
        for (const auto& q : building_->points) {
            if (!q.visible) {
                continue;
            }
            const double distance = QLineF(e->localPos(), worldToScreen(q.east_m, q.north_m)).length();
            if (distance < best) {
                best = distance;
                id = q.id;
            }
        }
        const bool control = e->modifiers() & Qt::ControlModifier;
        if (id) {
            selectedEdge_.reset();
            emit edgeSelected(-1);
            if (!control)
                selectedBuildings_.clear();
            if (control && selectedBuildings_.count(id))
                selectedBuildings_.erase(id);
            else
                selectedBuildings_.insert(id);
            emit buildingSelectionChanged(selectedBuildingList());
            update();
            return;
        }
        std::unordered_map<std::uint64_t, const SurveyPoint*> points;
        for (const auto& q : building_->points)
            points[q.id] = &q;
        int edgeIndex = -1;
        best = 8;
        for (size_t i = 0; i < building_->edges.size(); ++i) {
            const auto edge = building_->edges[i];
            if (!points.count(edge.a) || !points.count(edge.b))
                continue;
            const double distance = segmentDistance(
                                        e->localPos(), worldToScreen(points[edge.a]->east_m, points[edge.a]->north_m),
                                        worldToScreen(points[edge.b]->east_m, points[edge.b]->north_m));
            if (distance < best) {
                best = distance;
                edgeIndex = int(i);
            }
        }
        if (edgeIndex >= 0) {
            selectedBuildings_.clear();
            selectedEdge_ = edgeIndex;
            emit buildingSelectionChanged(QList<qulonglong> {});
            emit edgeSelected(edgeIndex);
            update();
            return;
        }
        if (!control && !selectedBuildings_.empty()) {
            selectedBuildings_.clear();
            emit buildingSelectionChanged(QList<qulonglong> {});
            update();
        }
    }
}

void SpatialWidget::mouseMoveEvent(QMouseEvent* e) {
    if (draggingInterpolationRegion_) {
        interpolationRegion_ =
            QRectF(interpolationDragStart_, screenToWorld(e->localPos())).normalized();
        update();
        return;
    }
    if (!panning_)
        return;
    pan_ += e->pos() - last_;
    last_ = e->pos();
    update();
}

void SpatialWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && draggingInterpolationRegion_) {
        draggingInterpolationRegion_ = false;
        selectingInterpolationRegion_ = false;
        setCursor(placement_ ? Qt::CrossCursor : Qt::ArrowCursor);
        if (interpolationRegion_.width() > 1e-6 && interpolationRegion_.height() > 1e-6) {
            emit interpolationRegionSelected(interpolationRegion_.normalized());
        }
        update();
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(placement_ ? Qt::CrossCursor : Qt::ArrowCursor);
    }
}

void SpatialWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton)
        resetView();
}

}  // namespace b210post
