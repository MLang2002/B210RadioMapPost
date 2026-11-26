#include "RasterInpainting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

namespace b210post {
namespace {

struct SourceSample {
    int dx = 0;
    int dy = 0;
    double power_dbfs = 0.0;
    double confidence = 0.0;
    double weight = 0.0;
};

bool dimensionsValid(const RadioRasterGrid& grid, const std::vector<double>& power,
                     const std::vector<RasterCellState>& state) {
    return grid.width_px > 0 && grid.height_px > 0 && grid.cellCount() == power.size() &&
           grid.cellCount() == state.size() && grid.cellCount() == grid.building_mask.size() &&
           grid.cellCount() == grid.sample_count.size();
}

double stateConfidence(RasterCellState state) {
    switch (state) {
    case RasterCellState::MeasuredAggregate:
    case RasterCellState::ManualRasterValue:
        return 1.0;
    case RasterCellState::RasterInterpolated:
        return 0.85;
    case RasterCellState::NearestFallback:
        return 0.65;
    case RasterCellState::AssistedProgressive:
        return 0.70;
    case RasterCellState::Missing:
    case RasterCellState::Building:
        return 0.0;
    }
    return 0.0;
}

bool lineCrossesBuilding(const RadioRasterGrid& grid, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int x = x0;
    int y = y0;
    while (true) {
        if (!(x == x0 && y == y0)) {
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

bool solveThreeByThree(std::array<std::array<double, 3>, 3> matrix, std::array<double, 3> rhs,
                       std::array<double, 3>* solution) {
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 3; ++row) {
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot])) {
                best = row;
            }
        }
        if (std::abs(matrix[best][pivot]) < 1e-10) {
            return false;
        }
        std::swap(matrix[pivot], matrix[best]);
        std::swap(rhs[pivot], rhs[best]);
        const double divisor = matrix[pivot][pivot];
        for (int column = pivot; column < 3; ++column) {
            matrix[pivot][column] /= divisor;
        }
        rhs[pivot] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) {
                continue;
            }
            const double factor = matrix[row][pivot];
            for (int column = pivot; column < 3; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
            rhs[row] -= factor * rhs[pivot];
        }
    }
    *solution = rhs;
    return true;
}

std::vector<SourceSample> collectLocalSources(const RadioRasterGrid& grid,
        const std::vector<double>& power,
        const std::vector<RasterCellState>& state,
        const std::vector<double>& confidence, int targetX,
        int targetY,
        const RasterInpaintingSettings& settings) {
    std::vector<SourceSample> sources;
    const int radius = settings.neighborhood_radius_px;
    for (int y = std::max(0, targetY - radius); y <= std::min(grid.height_px - 1, targetY + radius);
            ++y) {
        for (int x = std::max(0, targetX - radius); x <= std::min(grid.width_px - 1, targetX + radius);
                ++x) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            if (state[index] == RasterCellState::Missing || state[index] == RasterCellState::Building ||
                    !std::isfinite(power[index]) || lineCrossesBuilding(grid, x, y, targetX, targetY)) {
                continue;
            }
            const int dx = x - targetX;
            const int dy = y - targetY;
            const double distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
            if (distance < 1e-9 || distance > radius + 1e-9) {
                continue;
            }
            const double sourceConfidence = confidence[index];
            if (sourceConfidence <= 0.0) {
                continue;
            }
            const double weight =
                sourceConfidence / std::pow(std::max(0.5, distance), settings.distance_exponent);
            sources.push_back({dx, dy, power[index], sourceConfidence, weight});
        }
    }
    return sources;
}

RasterPowerSuggestion estimate(const std::vector<SourceSample>& sources,
                               const RasterInpaintingSettings& settings) {
    RasterPowerSuggestion suggestion;
    if (static_cast<int>(sources.size()) < settings.minimum_sources) {
        return suggestion;
    }
    std::array<std::array<double, 3>, 3> normal{};
    std::array<double, 3> rhs{};
    double totalWeight = 0.0;
    double weightedConfidence = 0.0;
    std::vector<double> powers;
    powers.reserve(sources.size());
    for (const auto& source : sources) {
        const std::array<double, 3> row = {1.0, static_cast<double>(source.dx),
                                           static_cast<double>(source.dy)
                                          };
        for (int i = 0; i < 3; ++i) {
            rhs[i] += source.weight * row[i] * source.power_dbfs;
            for (int j = 0; j < 3; ++j) {
                normal[i][j] += source.weight * row[i] * row[j];
            }
        }
        totalWeight += source.weight;
        weightedConfidence += source.weight * source.confidence;
        powers.push_back(source.power_dbfs);
    }
    std::array<double, 3> coefficients{};
    const bool planeValid = solveThreeByThree(normal, rhs, &coefficients);
    double prediction = 0.0;
    if (planeValid) {
        prediction = coefficients[0];
        suggestion.model = QStringLiteral("local weighted plane");
    } else {
        for (const auto& source : sources) {
            prediction += source.weight * source.power_dbfs;
        }
        prediction /= std::max(1e-12, totalWeight);
        suggestion.model = QStringLiteral("weighted local constant");
    }
    std::sort(powers.begin(), powers.end());
    const auto quantile = [&powers](double position) {
        const auto index =
            static_cast<std::size_t>(std::lround(position * static_cast<double>(powers.size() - 1)));
        return powers[std::min(index, powers.size() - 1)];
    };
    prediction = std::clamp(prediction, quantile(0.05) - settings.clamp_margin_db,
                            quantile(0.95) + settings.clamp_margin_db);
    suggestion.valid = std::isfinite(prediction);
    suggestion.power_dbfs = prediction;
    suggestion.confidence = totalWeight > 0.0 ? weightedConfidence / totalWeight : 0.0;
    suggestion.source_count = static_cast<int>(sources.size());
    return suggestion;
}

std::vector<std::size_t> neighbors(std::size_t index, const RadioRasterGrid& grid,
                                   bool eightConnected) {
    const int x = static_cast<int>(index % grid.width_px);
    const int y = static_cast<int>(index / grid.width_px);
    std::vector<std::size_t> result;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx == 0 && dy == 0) || (!eightConnected && std::abs(dx) + std::abs(dy) != 1)) {
                continue;
            }
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < grid.width_px && ny < grid.height_px) {
                result.push_back(static_cast<std::size_t>(ny) * grid.width_px + nx);
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> targetMask(const RadioRasterGrid& grid,
                                     const std::vector<RasterCellState>& state,
                                     std::size_t seedIndex,
                                     const RasterInpaintingSettings& settings) {
    std::vector<std::uint8_t> selected(grid.cellCount(), 0);
    if (settings.fill_all_components) {
        for (std::size_t index = 0; index < grid.cellCount(); ++index) {
            selected[index] = state[index] == RasterCellState::Missing &&
                              grid.building_mask[index] == 0 && grid.sample_count[index] == 0
                              ? 1
                              : 0;
        }
        return selected;
    }
    if (seedIndex >= grid.cellCount() || state[seedIndex] != RasterCellState::Missing ||
            grid.building_mask[seedIndex] != 0 || grid.sample_count[seedIndex] != 0) {
        return selected;
    }
    std::queue<std::size_t> pending;
    pending.push(seedIndex);
    selected[seedIndex] = 1;
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto adjacent : neighbors(current, grid, settings.use_eight_connectivity)) {
            if (!selected[adjacent] && state[adjacent] == RasterCellState::Missing &&
                    grid.building_mask[adjacent] == 0 && grid.sample_count[adjacent] == 0) {
                selected[adjacent] = 1;
                pending.push(adjacent);
            }
        }
    }
    return selected;
}

}  // namespace

QJsonObject RasterInpaintingSettings::toJson() const {
    return {{"method", QStringLiteral("direction_aware_progressive_local_plane")},
        {"neighborhood_radius_px", neighborhood_radius_px},
        {"minimum_sources", minimum_sources},
        {"distance_exponent", distance_exponent},
        {"confidence_decay_layers", confidence_decay_layers},
        {"minimum_confidence", minimum_confidence},
        {"maximum_layers", maximum_layers},
        {"clamp_margin_db", clamp_margin_db},
        {"use_eight_connectivity", use_eight_connectivity},
        {"fill_all_components", fill_all_components}};
}

bool RasterInpainting::validate(const RasterInpaintingSettings& settings, QString* error) {
    if (settings.neighborhood_radius_px < 1 || settings.minimum_sources < 1 ||
            settings.distance_exponent <= 0.0 || settings.confidence_decay_layers <= 0.0 ||
            settings.minimum_confidence < 0.0 || settings.minimum_confidence > 1.0 ||
            settings.maximum_layers < 1 || settings.clamp_margin_db < 0.0) {
        if (error) {
            *error = QStringLiteral("Invalid progressive-fill neighborhood or confidence settings.");
        }
        return false;
    }
    return true;
}

RasterPowerSuggestion RasterInpainting::suggest(const RadioRasterGrid& grid,
        const std::vector<double>& powerDbfs,
        const std::vector<RasterCellState>& state,
        std::size_t targetIndex,
        const RasterInpaintingSettings& settings) {
    if (!dimensionsValid(grid, powerDbfs, state) || targetIndex >= grid.cellCount() ||
            grid.building_mask[targetIndex] != 0 || !validate(settings)) {
        return {};
    }
    std::vector<double> confidence(grid.cellCount(), 0.0);
    for (std::size_t index = 0; index < grid.cellCount(); ++index) {
        confidence[index] = stateConfidence(state[index]);
    }
    const int targetX = static_cast<int>(targetIndex % grid.width_px);
    const int targetY = static_cast<int>(targetIndex / grid.width_px);
    return estimate(
               collectLocalSources(grid, powerDbfs, state, confidence, targetX, targetY, settings),
               settings);
}

RasterInpaintingResult RasterInpainting::fill(const RadioRasterGrid& grid,
        const std::vector<double>& powerDbfs,
        const std::vector<RasterCellState>& state,
        std::size_t seedIndex,
        const RasterInpaintingSettings& settings) {
    RasterInpaintingResult result;
    QString validationError;
    if (!dimensionsValid(grid, powerDbfs, state) || !validate(settings, &validationError)) {
        result.warning = validationError.isEmpty()
                         ? QStringLiteral("Raster arrays have inconsistent dimensions.")
                         : validationError;
        return result;
    }
    const auto selected = targetMask(grid, state, seedIndex, settings);
    result.selected_missing_cells =
        static_cast<int>(std::count(selected.begin(), selected.end(), std::uint8_t{1}));
    if (result.selected_missing_cells == 0) {
        result.warning = QStringLiteral("Select a currently missing non-building raster cell.");
        return result;
    }
    std::vector<double> workingPower = powerDbfs;
    std::vector<RasterCellState> workingState = state;
    std::vector<double> confidence(grid.cellCount(), 0.0);
    for (std::size_t index = 0; index < grid.cellCount(); ++index) {
        confidence[index] = stateConfidence(workingState[index]);
    }
    for (int layer = 1; layer <= settings.maximum_layers; ++layer) {
        std::vector<RasterInpaintCell> proposals;
        for (std::size_t index = 0; index < grid.cellCount(); ++index) {
            if (!selected[index] || workingState[index] != RasterCellState::Missing) {
                continue;
            }
            bool isFrontier = false;
            for (const auto adjacent : neighbors(index, grid, settings.use_eight_connectivity)) {
                if (workingState[adjacent] != RasterCellState::Missing &&
                        workingState[adjacent] != RasterCellState::Building) {
                    isFrontier = true;
                    break;
                }
            }
            if (!isFrontier) {
                continue;
            }
            const int targetX = static_cast<int>(index % grid.width_px);
            const int targetY = static_cast<int>(index / grid.width_px);
            auto suggestion = estimate(collectLocalSources(grid, workingPower, workingState, confidence,
                                       targetX, targetY, settings),
                                       settings);
            if (!suggestion.valid) {
                continue;
            }
            suggestion.confidence *= std::exp(-1.0 / settings.confidence_decay_layers);
            if (suggestion.confidence < settings.minimum_confidence) {
                continue;
            }
            proposals.push_back({index, suggestion.power_dbfs, suggestion.confidence, layer});
        }
        if (proposals.empty()) {
            break;
        }
        for (const auto& proposal : proposals) {
            workingPower[proposal.index] = proposal.power_dbfs;
            workingState[proposal.index] = RasterCellState::AssistedProgressive;
            confidence[proposal.index] = proposal.confidence;
            result.cells.push_back(proposal);
        }
        result.completed_layers = layer;
    }
    result.remaining_missing_cells =
        result.selected_missing_cells - static_cast<int>(result.cells.size());
    if (!result.cells.empty()) {
        result.minimum_applied_confidence =
            std::min_element(result.cells.begin(), result.cells.end(),
        [](const RasterInpaintCell& left, const RasterInpaintCell& right) {
            return left.confidence < right.confidence;
        })
        ->confidence;
        result.mean_applied_confidence = std::accumulate(result.cells.begin(), result.cells.end(), 0.0,
        [](double sum, const RasterInpaintCell& cell) {
            return sum + cell.confidence;
        }) /
        result.cells.size();
    }
    if (result.remaining_missing_cells > 0) {
        result.warning =
            QStringLiteral("Some cells did not meet the configured support or confidence threshold.");
    }
    return result;
}

}  // namespace b210post
