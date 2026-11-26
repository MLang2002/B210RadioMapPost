#ifndef __B210_RASTER_COMPLETION_HPP
#define __B210_RASTER_COMPLETION_HPP

#include <QJsonObject>
#include <QStringList>
#include <cstdint>
#include <vector>

#include "RasterRenderer.hpp"

namespace b210post {

enum class RasterCompletionMethod { Idw = 0, OrdinaryKriging = 1 };
enum class RasterCellState : std::uint8_t {
    Missing = 0,
    MeasuredAggregate = 1,
    RasterInterpolated = 2,
    NearestFallback = 3,
    AssistedProgressive = 4,
    ManualRasterValue = 5,
    Building = 6
};

struct RasterCompletionSettings {
    RasterCompletionMethod method = RasterCompletionMethod::Idw;
    int neighbor_count = 8;
    int minimum_neighbors = 4;
    int minimum_quadrants = 2;
    double maximum_distance_m = 20.0;
    double idw_exponent = 2.0;
    bool reject_building_crossing = true;
    bool nearest_fallback = false;
    double fallback_maximum_distance_m = 0.0;
    QString variogram_model = QStringLiteral("spherical");
    double nugget_db2 = 0.1;
    double partial_sill_db2 = 16.0;
    double range_m = 12.0;
    double regularization = 0.001;
    double maximum_variance_db2 = 100.0;

    QJsonObject toJson() const;
};

struct RasterCompletionResult {
    std::vector<double> power_dbfs;
    std::vector<RasterCellState> state;
    int measured_cells = 0;
    int missing_cells = 0;
    int interpolated_cells = 0;
    int fallback_cells = 0;
    int rejected_support = 0;
    int rejected_barrier = 0;
    int rejected_variance = 0;
    QStringList warnings;
};

class RasterCompletion {
  public:
    static bool validate(const RasterCompletionSettings& settings, QString* error = nullptr);
    static RasterCompletionResult complete(const RadioRasterGrid& grid,
                                           const RasterCompletionSettings& settings);
};

QString rasterCellStateName(RasterCellState state);

}  // namespace b210post

#endif // __B210_RASTER_COMPLETION_HPP
