#include "RasterCompletion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace b210post {
namespace {

struct SourceCell {
    int x = 0;
    int y = 0;
    double power_dbfs = 0.0;
    double distance_m = 0.0;
};

bool lineCrossesBuilding(const RadioRasterGrid& grid, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int x = x0;
    int y = y0;
    while (true) {
        if (!(x == x0 && y == y0) && !(x == x1 && y == y1)) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            if (grid.building_mask[index] != 0) {
                return true;
            }
        }
        if (x == x1 && y == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y += sy;
        }
    }
    return false;
}

std::vector<SourceCell> collectSources(const RadioRasterGrid& grid) {
    std::vector<SourceCell> sources;
    for (int y = 0; y < grid.height_px; ++y) {
        for (int x = 0; x < grid.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            if (grid.building_mask[index] == 0 && grid.sample_count[index] > 0 &&
                    std::isfinite(grid.power_dbfs[index])) {
                sources.push_back({x, y, grid.power_dbfs[index], 0.0});
            }
        }
    }
    return sources;
}

std::vector<SourceCell> nearestSources(const RadioRasterGrid& grid,
                                       const std::vector<SourceCell>& sources, int targetX,
                                       int targetY, const RasterCompletionSettings& settings,
                                       bool* barrierRejected) {
    std::vector<SourceCell> neighbors;
    neighbors.reserve(sources.size());
    for (auto source : sources) {
        const double dx = static_cast<double>(source.x - targetX);
        const double dy = static_cast<double>(source.y - targetY);
        source.distance_m = std::hypot(dx, dy) * grid.meters_per_pixel;
        if (settings.maximum_distance_m > 0.0 && source.distance_m > settings.maximum_distance_m) {
            continue;
        }
        if (settings.reject_building_crossing &&
                lineCrossesBuilding(grid, source.x, source.y, targetX, targetY)) {
            *barrierRejected = true;
            continue;
        }
        neighbors.push_back(source);
    }
    const int keep = std::min(settings.neighbor_count, static_cast<int>(neighbors.size()));
    if (keep < static_cast<int>(neighbors.size())) {
        std::nth_element(neighbors.begin(), neighbors.begin() + keep, neighbors.end(),
        [](const SourceCell& left, const SourceCell& right) {
            return left.distance_m < right.distance_m;
        });
        neighbors.resize(static_cast<std::size_t>(keep));
    }
    std::sort(neighbors.begin(), neighbors.end(),
    [](const SourceCell& left, const SourceCell& right) {
        return left.distance_m < right.distance_m;
    });
    return neighbors;
}

int occupiedQuadrants(const std::vector<SourceCell>& neighbors, int targetX, int targetY) {
    bool occupied[4] = {false, false, false, false};
    for (const auto& source : neighbors) {
        const int quadrant = (source.x >= targetX ? 1 : 0) + (source.y >= targetY ? 2 : 0);
        occupied[quadrant] = true;
    }
    return static_cast<int>(occupied[0]) + static_cast<int>(occupied[1]) +
           static_cast<int>(occupied[2]) + static_cast<int>(occupied[3]);
}

double semivariogram(double distance, const RasterCompletionSettings& settings) {
    if (distance <= 1e-12) {
        return 0.0;
    }
    const double ratio = distance / settings.range_m;
    if (settings.variogram_model == QStringLiteral("exponential")) {
        return settings.nugget_db2 + settings.partial_sill_db2 * (1.0 - std::exp(-3.0 * ratio));
    }
    if (settings.variogram_model == QStringLiteral("gaussian")) {
        return settings.nugget_db2 + settings.partial_sill_db2 * (1.0 - std::exp(-3.0 * ratio * ratio));
    }
    if (ratio >= 1.0) {
        return settings.nugget_db2 + settings.partial_sill_db2;
    }
    return settings.nugget_db2 +
           settings.partial_sill_db2 * (1.5 * ratio - 0.5 * ratio * ratio * ratio);
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

bool idw(const std::vector<SourceCell>& neighbors, const RasterCompletionSettings& settings,
         double* powerDbfs) {
    double weightedPower = 0.0;
    double totalWeight = 0.0;
    for (const auto& source : neighbors) {
        const double weight = 1.0 / std::pow(std::max(1e-6, source.distance_m), settings.idw_exponent);
        weightedPower += weight * std::pow(10.0, source.power_dbfs / 10.0);
        totalWeight += weight;
    }
    if (!(weightedPower > 0.0 && totalWeight > 0.0)) {
        return false;
    }
    *powerDbfs = 10.0 * std::log10(weightedPower / totalWeight);
    return true;
}

bool ordinaryKriging(const std::vector<SourceCell>& neighbors, int targetX, int targetY,
                     const RadioRasterGrid& grid, const RasterCompletionSettings& settings,
                     double* powerDbfs, double* variance) {
    const int count = static_cast<int>(neighbors.size());
    std::vector<std::vector<double>> matrix(count + 1, std::vector<double>(count + 1, 0.0));
    std::vector<double> rhs(count + 1, 1.0);
    for (int row = 0; row < count; ++row) {
        for (int column = 0; column < count; ++column) {
            const double distance =
                std::hypot(static_cast<double>(neighbors[row].x - neighbors[column].x),
                           static_cast<double>(neighbors[row].y - neighbors[column].y)) *
                grid.meters_per_pixel;
            matrix[row][column] = semivariogram(distance, settings);
        }
        matrix[row][row] += settings.regularization;
        matrix[row][count] = 1.0;
        matrix[count][row] = 1.0;
        const double targetDistance = std::hypot(static_cast<double>(neighbors[row].x - targetX),
                                      static_cast<double>(neighbors[row].y - targetY)) *
                                      grid.meters_per_pixel;
        rhs[row] = semivariogram(targetDistance, settings);
    }
    std::vector<double> weights;
    if (!solveLinearSystem(matrix, rhs, &weights)) {
        return false;
    }
    *powerDbfs = 0.0;
    *variance = weights[count];
    for (int index = 0; index < count; ++index) {
        *powerDbfs += weights[index] * neighbors[index].power_dbfs;
        *variance += weights[index] * rhs[index];
    }
    *variance = std::max(0.0, *variance);
    return std::isfinite(*powerDbfs) && std::isfinite(*variance);
}

bool nearestFallback(const RadioRasterGrid& grid, const std::vector<SourceCell>& sources,
                     int targetX, int targetY, const RasterCompletionSettings& settings,
                     double* powerDbfs) {
    double bestDistance = std::numeric_limits<double>::infinity();
    const SourceCell* best = nullptr;
    for (const auto& source : sources) {
        const double distance = std::hypot(static_cast<double>(source.x - targetX),
                                           static_cast<double>(source.y - targetY)) *
                                grid.meters_per_pixel;
        if (settings.fallback_maximum_distance_m > 0.0 &&
                distance > settings.fallback_maximum_distance_m) {
            continue;
        }
        if (settings.reject_building_crossing &&
                lineCrossesBuilding(grid, source.x, source.y, targetX, targetY)) {
            continue;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &source;
        }
    }
    if (!best) {
        return false;
    }
    *powerDbfs = best->power_dbfs;
    return true;
}

}  // namespace

QJsonObject RasterCompletionSettings::toJson() const {
    return {{"method", method == RasterCompletionMethod::Idw ? "idw" : "ordinary_kriging"},
        {"neighbor_count", neighbor_count},
        {"minimum_neighbors", minimum_neighbors},
        {"minimum_quadrants", minimum_quadrants},
        {"maximum_distance_m", maximum_distance_m},
        {"idw_exponent", idw_exponent},
        {"reject_building_crossing", reject_building_crossing},
        {"nearest_fallback", nearest_fallback},
        {"fallback_maximum_distance_m", fallback_maximum_distance_m},
        {"variogram_model", variogram_model},
        {"nugget_db2", nugget_db2},
        {"partial_sill_db2", partial_sill_db2},
        {"range_m", range_m},
        {"regularization", regularization},
        {"maximum_variance_db2", maximum_variance_db2}};
}

bool RasterCompletion::validate(const RasterCompletionSettings& settings, QString* error) {
    if (settings.neighbor_count < 1 || settings.minimum_neighbors < 1 ||
            settings.minimum_neighbors > settings.neighbor_count || settings.minimum_quadrants < 1 ||
            settings.minimum_quadrants > 4 || settings.maximum_distance_m < 0.0 ||
            settings.idw_exponent <= 0.0 || settings.fallback_maximum_distance_m < 0.0) {
        if (error) {
            *error = QStringLiteral("Invalid neighborhood or distance parameters.");
        }
        return false;
    }
    if (settings.method == RasterCompletionMethod::OrdinaryKriging &&
            (settings.nugget_db2 < 0.0 || settings.partial_sill_db2 <= 0.0 || settings.range_m <= 0.0 ||
             settings.regularization < 0.0 || settings.maximum_variance_db2 < 0.0)) {
        if (error) {
            *error = QStringLiteral("Invalid ordinary-kriging parameters.");
        }
        return false;
    }
    return true;
}

RasterCompletionResult RasterCompletion::complete(const RadioRasterGrid& grid,
        const RasterCompletionSettings& settings) {
    RasterCompletionResult result;
    result.power_dbfs = grid.power_dbfs;
    result.state.assign(grid.cellCount(), RasterCellState::Missing);
    if (grid.cellCount() == 0 || grid.power_dbfs.size() != grid.cellCount() ||
            grid.sample_count.size() != grid.cellCount() ||
            grid.building_mask.size() != grid.cellCount()) {
        result.warnings << QStringLiteral("Radio raster arrays have inconsistent dimensions.");
        return result;
    }
    QString validationError;
    if (!validate(settings, &validationError)) {
        result.warnings << validationError;
        return result;
    }
    const auto sources = collectSources(grid);
    for (int y = 0; y < grid.height_px; ++y) {
        for (int x = 0; x < grid.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            if (grid.building_mask[index] != 0) {
                result.state[index] = RasterCellState::Building;
            } else if (grid.sample_count[index] > 0) {
                result.state[index] = RasterCellState::MeasuredAggregate;
                ++result.measured_cells;
                if (!std::isfinite(grid.power_dbfs[index])) {
                    result.warnings << QStringLiteral(
                                        "A measured raster cell has non-finite power and was left unchanged.");
                }
            } else {
                ++result.missing_cells;
            }
        }
    }
    if (sources.empty()) {
        result.warnings << QStringLiteral("No non-building measured raster cell is available.");
        return result;
    }
    for (int y = 0; y < grid.height_px; ++y) {
        for (int x = 0; x < grid.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            if (result.state[index] != RasterCellState::Missing) {
                continue;
            }
            bool barrierRejected = false;
            const auto neighbors = nearestSources(grid, sources, x, y, settings, &barrierRejected);
            double prediction = std::numeric_limits<double>::quiet_NaN();
            bool predicted = false;
            if (static_cast<int>(neighbors.size()) >= settings.minimum_neighbors &&
                    occupiedQuadrants(neighbors, x, y) >= settings.minimum_quadrants) {
                if (settings.method == RasterCompletionMethod::Idw) {
                    predicted = idw(neighbors, settings, &prediction);
                } else {
                    double variance = 0.0;
                    predicted = ordinaryKriging(neighbors, x, y, grid, settings, &prediction, &variance);
                    if (predicted && variance > settings.maximum_variance_db2) {
                        predicted = false;
                        ++result.rejected_variance;
                    }
                }
            } else if (barrierRejected) {
                ++result.rejected_barrier;
            } else {
                ++result.rejected_support;
            }
            if (predicted) {
                result.power_dbfs[index] = prediction;
                result.state[index] = RasterCellState::RasterInterpolated;
                ++result.interpolated_cells;
            } else if (settings.nearest_fallback &&
                       nearestFallback(grid, sources, x, y, settings, &prediction)) {
                result.power_dbfs[index] = prediction;
                result.state[index] = RasterCellState::NearestFallback;
                ++result.fallback_cells;
            }
        }
    }
    return result;
}

QString rasterCellStateName(RasterCellState state) {
    switch (state) {
    case RasterCellState::Missing:
        return QStringLiteral("Missing");
    case RasterCellState::MeasuredAggregate:
        return QStringLiteral("Measured aggregate");
    case RasterCellState::RasterInterpolated:
        return QStringLiteral("Raster interpolated");
    case RasterCellState::NearestFallback:
        return QStringLiteral("Nearest fallback");
    case RasterCellState::AssistedProgressive:
        return QStringLiteral("Assisted progressive fill");
    case RasterCellState::ManualRasterValue:
        return QStringLiteral("Manual raster value");
    case RasterCellState::Building:
        return QStringLiteral("Building (locked)");
    }
    return {};
}

}  // namespace b210post
