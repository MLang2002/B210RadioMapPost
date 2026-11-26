#include "Interpolation.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineF>
#include <QPolygonF>
#include <QSpinBox>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

#include "RasterRenderer.hpp"

namespace b210post {
namespace {

struct SourceSample {
    QPointF position;
    double power_dbfs = 0.0;
    double linear_power = 0.0;
    double distance = 0.0;
};

struct GeometryContext {
    std::vector<QPolygonF> polygons;
    std::vector<QLineF> edges;
};

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

double cross(const QPointF& a, const QPointF& b, const QPointF& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

bool properIntersection(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d) {
    const double ab1 = cross(a, b, c);
    const double ab2 = cross(a, b, d);
    const double cd1 = cross(c, d, a);
    const double cd2 = cross(c, d, b);
    constexpr double epsilon = 1e-9;
    return ((ab1 > epsilon && ab2 < -epsilon) || (ab1 < -epsilon && ab2 > epsilon)) &&
           ((cd1 > epsilon && cd2 < -epsilon) || (cd1 < -epsilon && cd2 > epsilon));
}

GeometryContext buildGeometry(const BuildingDataset* building) {
    GeometryContext geometry;
    if (!building) {
        return geometry;
    }
    const BuildingValidation validation = RasterRenderer::validateBuilding(*building);
    if (!validation.valid) {
        return geometry;
    }
    std::unordered_map<std::uint64_t, QPointF> points;
    for (const auto& point : building->points) {
        points.emplace(point.id, QPointF(point.east_m, point.north_m));
    }
    for (const auto& loop : validation.loops) {
        QPolygonF polygon;
        for (const auto id : loop) {
            polygon << points.at(id);
        }
        geometry.polygons.push_back(polygon);
        for (int index = 0; index < polygon.size(); ++index) {
            geometry.edges.emplace_back(polygon[index], polygon[(index + 1) % polygon.size()]);
        }
    }
    return geometry;
}

bool insideBuilding(const QPointF& point, const GeometryContext& geometry) {
    for (const auto& polygon : geometry.polygons) {
        if (polygon.containsPoint(point, Qt::OddEvenFill)) {
            return true;
        }
    }
    return false;
}

bool crossesBuilding(const QPointF& source, const QPointF& target,
                     const GeometryContext& geometry) {
    for (const auto& edge : geometry.edges) {
        if (properIntersection(source, target, edge.p1(), edge.p2())) {
            return true;
        }
    }
    return false;
}

std::vector<SourceSample> collectSources(const InterpolationRequest& request) {
    std::vector<SourceSample> sources;
    if (!request.radio) {
        return sources;
    }
    for (const auto& point : request.radio->points) {
        if (point.role != RadioRole::Rx || !point.valid ||
                point.generation_kind == PointGenerationKind::Interpolated ||
                !std::isfinite(point.east_m) || !std::isfinite(point.north_m) ||
                !std::isfinite(point.power_dbfs) || !request.region.contains(point.east_m, point.north_m)) {
            continue;
        }
        sources.push_back({QPointF(point.east_m, point.north_m), point.power_dbfs,
                           std::pow(10.0, point.power_dbfs / 10.0), 0.0});
    }
    return sources;
}

std::vector<SourceSample> prepareSources(const InterpolationRequest& request,
        const GeometryContext& geometry, double binSize) {
    struct Bin {
        double east_sum = 0.0;
        double north_sum = 0.0;
        double linear_power_sum = 0.0;
        int count = 0;
    };
    std::map<std::pair<long long, long long>, Bin> bins;
    for (const auto& source : collectSources(request)) {
        if (insideBuilding(source.position, geometry)) {
            continue;
        }
        const auto key =
            std::make_pair(static_cast<long long>(std::floor(source.position.x() / binSize)),
                           static_cast<long long>(std::floor(source.position.y() / binSize)));
        auto& bin = bins[key];
        bin.east_sum += source.position.x();
        bin.north_sum += source.position.y();
        bin.linear_power_sum += source.linear_power;
        ++bin.count;
    }
    std::vector<SourceSample> sources;
    sources.reserve(bins.size());
    for (const auto& entry : bins) {
        const auto& bin = entry.second;
        const double linearPower = bin.linear_power_sum / bin.count;
        sources.push_back({QPointF(bin.east_sum / bin.count, bin.north_sum / bin.count),
                           10.0 * std::log10(linearPower), linearPower, 0.0});
    }
    return sources;
}

std::vector<QPointF> buildTargets(const QRectF& rawRegion, double spacing) {
    const QRectF region = rawRegion.normalized();
    std::vector<QPointF> targets;
    const double firstEast = std::ceil(region.left() / spacing) * spacing;
    const double firstNorth = std::ceil(region.top() / spacing) * spacing;
    for (double north = firstNorth; north <= region.bottom() + 1e-9; north += spacing) {
        for (double east = firstEast; east <= region.right() + 1e-9; east += spacing) {
            targets.emplace_back(east, north);
        }
    }
    return targets;
}

bool targetCountIsSafe(const QRectF& rawRegion, double spacing, InterpolationResult* result) {
    const QRectF region = rawRegion.normalized();
    const double columns = std::floor(region.width() / spacing) + 2.0;
    const double rows = std::floor(region.height() / spacing) + 2.0;
    if (columns * rows <= 1000000.0) {
        return true;
    }
    result->warnings << QStringLiteral(
                         "The region/spacing combination exceeds 1,000,000 candidates. Increase target spacing "
                         "or select a smaller region.");
    return false;
}

bool isNearExisting(const QPointF& target, const std::vector<SourceSample>& sources,
                    double exclusionRadius) {
    const double squaredRadius = exclusionRadius * exclusionRadius;
    for (const auto& source : sources) {
        const QPointF delta = source.position - target;
        if (QPointF::dotProduct(delta, delta) <= squaredRadius) {
            return true;
        }
    }
    return false;
}

std::vector<SourceSample> nearestVisibleSources(const QPointF& target,
        const std::vector<SourceSample>& allSources,
        const GeometryContext& geometry, int neighborCount,
        double maximumRadius, bool* barrierRejected) {
    std::vector<SourceSample> neighbors;
    const double squaredRadius = maximumRadius * maximumRadius;
    for (auto source : allSources) {
        const QPointF delta = source.position - target;
        const double squaredDistance = QPointF::dotProduct(delta, delta);
        if (squaredDistance > squaredRadius) {
            continue;
        }
        if (crossesBuilding(source.position, target, geometry)) {
            if (barrierRejected) {
                *barrierRejected = true;
            }
            continue;
        }
        source.distance = std::sqrt(squaredDistance);
        neighbors.push_back(source);
    }
    std::sort(neighbors.begin(), neighbors.end(), [](const SourceSample& a, const SourceSample& b) {
        return a.distance < b.distance;
    });
    if (static_cast<int>(neighbors.size()) > neighborCount) {
        neighbors.resize(static_cast<std::size_t>(neighborCount));
    }
    return neighbors;
}

int occupiedQuadrants(const QPointF& target, const std::vector<SourceSample>& neighbors) {
    bool occupied[4] = {false, false, false, false};
    for (const auto& source : neighbors) {
        const QPointF delta = source.position - target;
        const int quadrant = (delta.x() >= 0.0 ? 1 : 0) + (delta.y() >= 0.0 ? 2 : 0);
        occupied[quadrant] = true;
    }
    return static_cast<int>(occupied[0]) + static_cast<int>(occupied[1]) +
           static_cast<int>(occupied[2]) + static_cast<int>(occupied[3]);
}

bool commonParametersValid(const QJsonObject& parameters, QString* error) {
    const double spacing = parameters.value("target_spacing_m").toDouble();
    const double exclusion = parameters.value("existing_exclusion_radius_m").toDouble();
    const int neighbors = parameters.value("neighbor_count").toInt();
    const int minimum = parameters.value("minimum_neighbors").toInt();
    const double radius = parameters.value("maximum_radius_m").toDouble();
    if (!(spacing > 0.0 && exclusion >= 0.0 && neighbors >= 1 && minimum >= 1 &&
            minimum <= neighbors && radius > 0.0)) {
        if (error) {
            *error = QStringLiteral(
                         "Spacing/radius must be positive, exclusion non-negative, and minimum "
                         "neighbors must not exceed neighbor count.");
        }
        return false;
    }
    return true;
}

class IdwParameterWidget final : public InterpolationParameterWidget {
  public:
    explicit IdwParameterWidget(QWidget* parent) : InterpolationParameterWidget(parent) {
        spacing_ = doubleSpin(0.01, 1000.0, 1.0, 3, "Distance between generated candidate points.");
        exclusion_ = doubleSpin(0.0, 1000.0, 0.5, 3,
                                "Do not generate a point this close to an original RX sample.");
        neighbors_ = integerSpin(1, 128, 8, "Maximum nearest source samples used per target.");
        minimumNeighbors_ =
            integerSpin(1, 128, 4, "Minimum usable source samples required per target.");
        maximumRadius_ = doubleSpin(0.01, 100000.0, 10.0, 3,
                                    "Maximum distance from a target to an eligible source sample.");
        exponent_ =
            doubleSpin(0.1, 10.0, 2.0, 2, "IDW distance exponent. Larger values favor nearby samples.");
        minimumQuadrants_ = integerSpin(
                                1, 4, 3, "Required occupied quadrants around a target; reduces one-sided extrapolation.");
        auto* form = new QFormLayout(this);
        form->addRow("Target spacing [m]", spacing_);
        form->addRow("Existing exclusion [m]", exclusion_);
        form->addRow("Neighbor count", neighbors_);
        form->addRow("Minimum neighbors", minimumNeighbors_);
        form->addRow("Maximum radius [m]", maximumRadius_);
        form->addRow("Distance exponent", exponent_);
        form->addRow("Minimum quadrants", minimumQuadrants_);
    }

    QJsonObject parameters() const override {
        return {{"target_spacing_m", spacing_->value()},
            {"existing_exclusion_radius_m", exclusion_->value()},
            {"neighbor_count", neighbors_->value()},
            {"minimum_neighbors", minimumNeighbors_->value()},
            {"maximum_radius_m", maximumRadius_->value()},
            {"distance_exponent", exponent_->value()},
            {"minimum_quadrants", minimumQuadrants_->value()}};
    }

  private:
    QDoubleSpinBox* spacing_ = nullptr;
    QDoubleSpinBox* exclusion_ = nullptr;
    QSpinBox* neighbors_ = nullptr;
    QSpinBox* minimumNeighbors_ = nullptr;
    QDoubleSpinBox* maximumRadius_ = nullptr;
    QDoubleSpinBox* exponent_ = nullptr;
    QSpinBox* minimumQuadrants_ = nullptr;
};

class IdwMethod final : public InterpolationMethod {
  public:
    QString id() const override {
        return QStringLiteral("idw");
    }

    QString displayName() const override {
        return QStringLiteral("Local IDW");
    }

    QString description() const override {
        return QStringLiteral(
                   "Inverse-distance weighting in linear power. Building interiors and "
                   "source-target paths crossing building boundaries are rejected.");
    }

    std::unique_ptr<InterpolationParameterWidget> createParameterWidget(
        QWidget* parent) const override {
        return std::make_unique<IdwParameterWidget>(parent);
    }

    bool validateParameters(const QJsonObject& parameters, QString* error) const override {
        if (!commonParametersValid(parameters, error)) {
            return false;
        }
        const double exponent = parameters.value("distance_exponent").toDouble();
        const int quadrants = parameters.value("minimum_quadrants").toInt();
        if (!(exponent > 0.0 && quadrants >= 1 && quadrants <= 4)) {
            if (error) {
                *error = QStringLiteral("IDW exponent must be positive and quadrants must be 1-4.");
            }
            return false;
        }
        return true;
    }

    InterpolationResult interpolate(const InterpolationRequest& request) const override {
        InterpolationResult result;
        const auto geometry = buildGeometry(request.building);
        const double spacing = request.parameters.value("target_spacing_m").toDouble();
        const auto sources = prepareSources(request, geometry, spacing);
        const double exclusion = request.parameters.value("existing_exclusion_radius_m").toDouble();
        const int neighborCount = request.parameters.value("neighbor_count").toInt();
        const int minimumNeighbors = request.parameters.value("minimum_neighbors").toInt();
        const double maximumRadius = request.parameters.value("maximum_radius_m").toDouble();
        const double exponent = request.parameters.value("distance_exponent").toDouble();
        const int minimumQuadrants = request.parameters.value("minimum_quadrants").toInt();
        if (!targetCountIsSafe(request.region, spacing, &result)) {
            return result;
        }
        const auto targets = buildTargets(request.region, spacing);
        result.candidate_count = static_cast<int>(targets.size());
        if (sources.empty()) {
            result.warnings << QStringLiteral("No eligible original RX samples exist in the region.");
            return result;
        }
        for (const auto& target : targets) {
            if (isNearExisting(target, sources, exclusion)) {
                ++result.skipped_near_existing;
                continue;
            }
            if (insideBuilding(target, geometry)) {
                ++result.skipped_building;
                continue;
            }
            bool barrierRejected = false;
            const auto neighbors = nearestVisibleSources(target, sources, geometry, neighborCount,
                                   maximumRadius, &barrierRejected);
            if (static_cast<int>(neighbors.size()) < minimumNeighbors ||
                    occupiedQuadrants(target, neighbors) < minimumQuadrants) {
                if (barrierRejected) {
                    ++result.skipped_barrier;
                } else {
                    ++result.skipped_support;
                }
                continue;
            }
            double weightedPower = 0.0;
            double totalWeight = 0.0;
            for (const auto& neighbor : neighbors) {
                const double weight = 1.0 / std::pow(std::max(1e-6, neighbor.distance), exponent);
                weightedPower += weight * neighbor.linear_power;
                totalWeight += weight;
            }
            if (weightedPower > 0.0 && totalWeight > 0.0) {
                result.samples.push_back({target, 10.0 * std::log10(weightedPower / totalWeight), 0.0});
            }
        }
        return result;
    }
};

class KrigingParameterWidget final : public InterpolationParameterWidget {
  public:
    explicit KrigingParameterWidget(QWidget* parent) : InterpolationParameterWidget(parent) {
        spacing_ = doubleSpin(0.01, 1000.0, 1.0, 3, "Distance between generated candidate points.");
        exclusion_ = doubleSpin(0.0, 1000.0, 0.5, 3,
                                "Do not generate a point this close to an original RX sample.");
        neighbors_ = integerSpin(2, 64, 12, "Maximum local kriging system size.");
        minimumNeighbors_ = integerSpin(2, 64, 6, "Minimum samples required for kriging.");
        maximumRadius_ =
            doubleSpin(0.01, 100000.0, 15.0, 3, "Maximum source distance from each target.");
        minimumQuadrants_ = integerSpin(1, 4, 3, "Required occupied source quadrants.");
        model_ = new QComboBox;
        model_->addItem("Spherical", "spherical");
        model_->addItem("Exponential", "exponential");
        model_->addItem("Gaussian", "gaussian");
        model_->setToolTip("Semivariogram model used by ordinary kriging.");
        nugget_ = doubleSpin(0.0, 10000.0, 0.1, 4, "Semivariogram nugget in squared dB units.");
        sill_ = doubleSpin(0.0001, 10000.0, 16.0, 4, "Partial sill in squared dB units.");
        range_ = doubleSpin(0.01, 100000.0, 12.0, 3, "Semivariogram correlation range in metres.");
        regularization_ =
            doubleSpin(0.0, 100.0, 0.001, 6, "Diagonal stabilization for nearly singular systems.");
        maximumVariance_ =
            doubleSpin(0.0, 100000.0, 100.0, 3,
                       "Reject predictions whose kriging variance exceeds this squared-dB threshold.");
        auto* form = new QFormLayout(this);
        form->addRow("Target spacing [m]", spacing_);
        form->addRow("Existing exclusion [m]", exclusion_);
        form->addRow("Neighbor count", neighbors_);
        form->addRow("Minimum neighbors", minimumNeighbors_);
        form->addRow("Maximum radius [m]", maximumRadius_);
        form->addRow("Minimum quadrants", minimumQuadrants_);
        form->addRow("Variogram", model_);
        form->addRow("Nugget [dB^2]", nugget_);
        form->addRow("Partial sill [dB^2]", sill_);
        form->addRow("Range [m]", range_);
        form->addRow("Regularization", regularization_);
        form->addRow("Maximum variance [dB^2]", maximumVariance_);
    }

    QJsonObject parameters() const override {
        return {{"target_spacing_m", spacing_->value()},
            {"existing_exclusion_radius_m", exclusion_->value()},
            {"neighbor_count", neighbors_->value()},
            {"minimum_neighbors", minimumNeighbors_->value()},
            {"maximum_radius_m", maximumRadius_->value()},
            {"minimum_quadrants", minimumQuadrants_->value()},
            {"variogram_model", model_->currentData().toString()},
            {"nugget_db2", nugget_->value()},
            {"partial_sill_db2", sill_->value()},
            {"range_m", range_->value()},
            {"regularization", regularization_->value()},
            {"maximum_variance_db2", maximumVariance_->value()}};
    }

  private:
    QDoubleSpinBox* spacing_ = nullptr;
    QDoubleSpinBox* exclusion_ = nullptr;
    QSpinBox* neighbors_ = nullptr;
    QSpinBox* minimumNeighbors_ = nullptr;
    QDoubleSpinBox* maximumRadius_ = nullptr;
    QSpinBox* minimumQuadrants_ = nullptr;
    QComboBox* model_ = nullptr;
    QDoubleSpinBox* nugget_ = nullptr;
    QDoubleSpinBox* sill_ = nullptr;
    QDoubleSpinBox* range_ = nullptr;
    QDoubleSpinBox* regularization_ = nullptr;
    QDoubleSpinBox* maximumVariance_ = nullptr;
};

double semivariogram(double distance, const QJsonObject& parameters) {
    const double nugget = parameters.value("nugget_db2").toDouble();
    const double sill = parameters.value("partial_sill_db2").toDouble();
    const double range = parameters.value("range_m").toDouble();
    const double ratio = distance / range;
    const QString model = parameters.value("variogram_model").toString();
    if (distance <= 1e-12) {
        return 0.0;
    }
    if (model == QStringLiteral("exponential")) {
        return nugget + sill * (1.0 - std::exp(-3.0 * ratio));
    }
    if (model == QStringLiteral("gaussian")) {
        return nugget + sill * (1.0 - std::exp(-3.0 * ratio * ratio));
    }
    if (ratio >= 1.0) {
        return nugget + sill;
    }
    return nugget + sill * (1.5 * ratio - 0.5 * ratio * ratio * ratio);
}

bool solveLinearSystem(std::vector<std::vector<double>> matrix, std::vector<double> rightHandSide,
                       std::vector<double>* solution) {
    const int size = static_cast<int>(rightHandSide.size());
    for (int pivot = 0; pivot < size; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < size; ++row) {
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot])) {
                best = row;
            }
        }
        if (std::abs(matrix[best][pivot]) < 1e-12) {
            return false;
        }
        std::swap(matrix[pivot], matrix[best]);
        std::swap(rightHandSide[pivot], rightHandSide[best]);
        const double divisor = matrix[pivot][pivot];
        for (int column = pivot; column < size; ++column) {
            matrix[pivot][column] /= divisor;
        }
        rightHandSide[pivot] /= divisor;
        for (int row = 0; row < size; ++row) {
            if (row == pivot) {
                continue;
            }
            const double factor = matrix[row][pivot];
            for (int column = pivot; column < size; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
            rightHandSide[row] -= factor * rightHandSide[pivot];
        }
    }
    *solution = std::move(rightHandSide);
    return true;
}

class KrigingMethod final : public InterpolationMethod {
  public:
    QString id() const override {
        return QStringLiteral("ordinary_kriging");
    }

    QString displayName() const override {
        return QStringLiteral("Local Ordinary Kriging");
    }

    QString description() const override {
        return QStringLiteral(
                   "Local ordinary kriging of dBFS values with a configurable semivariogram. "
                   "Predictions with excessive variance are rejected.");
    }

    std::unique_ptr<InterpolationParameterWidget> createParameterWidget(
        QWidget* parent) const override {
        return std::make_unique<KrigingParameterWidget>(parent);
    }

    bool validateParameters(const QJsonObject& parameters, QString* error) const override {
        if (!commonParametersValid(parameters, error)) {
            return false;
        }
        const QString model = parameters.value("variogram_model").toString();
        const bool knownModel = model == QStringLiteral("spherical") ||
                                model == QStringLiteral("exponential") ||
                                model == QStringLiteral("gaussian");
        const int quadrants = parameters.value("minimum_quadrants").toInt();
        if (!knownModel || quadrants < 1 || quadrants > 4 ||
                parameters.value("nugget_db2").toDouble() < 0.0 ||
                parameters.value("partial_sill_db2").toDouble() <= 0.0 ||
                parameters.value("range_m").toDouble() <= 0.0 ||
                parameters.value("regularization").toDouble() < 0.0 ||
                parameters.value("maximum_variance_db2").toDouble() < 0.0) {
            if (error) {
                *error = QStringLiteral("Invalid ordinary-kriging parameters.");
            }
            return false;
        }
        return true;
    }

    InterpolationResult interpolate(const InterpolationRequest& request) const override {
        InterpolationResult result;
        const auto geometry = buildGeometry(request.building);
        const double spacing = request.parameters.value("target_spacing_m").toDouble();
        const auto sources = prepareSources(request, geometry, spacing);
        const double exclusion = request.parameters.value("existing_exclusion_radius_m").toDouble();
        const int neighborCount = request.parameters.value("neighbor_count").toInt();
        const int minimumNeighbors = request.parameters.value("minimum_neighbors").toInt();
        const double maximumRadius = request.parameters.value("maximum_radius_m").toDouble();
        const int minimumQuadrants = request.parameters.value("minimum_quadrants").toInt();
        const double regularization = request.parameters.value("regularization").toDouble();
        const double maximumVariance = request.parameters.value("maximum_variance_db2").toDouble();
        if (!targetCountIsSafe(request.region, spacing, &result)) {
            return result;
        }
        const auto targets = buildTargets(request.region, spacing);
        result.candidate_count = static_cast<int>(targets.size());
        if (sources.empty()) {
            result.warnings << QStringLiteral("No eligible original RX samples exist in the region.");
            return result;
        }
        for (const auto& target : targets) {
            if (isNearExisting(target, sources, exclusion)) {
                ++result.skipped_near_existing;
                continue;
            }
            if (insideBuilding(target, geometry)) {
                ++result.skipped_building;
                continue;
            }
            bool barrierRejected = false;
            const auto neighbors = nearestVisibleSources(target, sources, geometry, neighborCount,
                                   maximumRadius, &barrierRejected);
            if (static_cast<int>(neighbors.size()) < minimumNeighbors ||
                    occupiedQuadrants(target, neighbors) < minimumQuadrants) {
                if (barrierRejected) {
                    ++result.skipped_barrier;
                } else {
                    ++result.skipped_support;
                }
                continue;
            }
            const int count = static_cast<int>(neighbors.size());
            std::vector<std::vector<double>> matrix(count + 1, std::vector<double>(count + 1, 0.0));
            std::vector<double> rhs(count + 1, 1.0);
            for (int row = 0; row < count; ++row) {
                for (int column = 0; column < count; ++column) {
                    matrix[row][column] =
                        semivariogram(QLineF(neighbors[row].position, neighbors[column].position).length(),
                                      request.parameters);
                }
                matrix[row][row] += regularization;
                matrix[row][count] = 1.0;
                matrix[count][row] = 1.0;
                rhs[row] =
                    semivariogram(QLineF(neighbors[row].position, target).length(), request.parameters);
            }
            matrix[count][count] = 0.0;
            rhs[count] = 1.0;
            std::vector<double> weights;
            if (!solveLinearSystem(matrix, rhs, &weights)) {
                ++result.skipped_support;
                continue;
            }
            double prediction = 0.0;
            double variance = weights[count];
            for (int index = 0; index < count; ++index) {
                prediction += weights[index] * neighbors[index].power_dbfs;
                variance += weights[index] * rhs[index];
            }
            variance = std::max(0.0, variance);
            if (!std::isfinite(prediction) || !std::isfinite(variance) || variance > maximumVariance) {
                ++result.skipped_uncertainty;
                continue;
            }
            result.samples.push_back({target, prediction, variance});
        }
        return result;
    }
};

}  // namespace

std::vector<std::unique_ptr<InterpolationMethod>> createBuiltInInterpolationMethods() {
    std::vector<std::unique_ptr<InterpolationMethod>> methods;
    methods.push_back(std::make_unique<IdwMethod>());
    methods.push_back(std::make_unique<KrigingMethod>());
    return methods;
}

}  // namespace b210post
