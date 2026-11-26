#ifndef __B210_RASTER_INPAINTING_HPP
#define __B210_RASTER_INPAINTING_HPP

#include <QJsonObject>
#include <QString>
#include <cstddef>
#include <vector>

#include "RasterCompletion.hpp"

namespace b210post {

struct RasterInpaintingSettings {
    int neighborhood_radius_px = 6;
    int minimum_sources = 3;
    double distance_exponent = 2.0;
    double confidence_decay_layers = 10.0;
    double minimum_confidence = 0.15;
    int maximum_layers = 256;
    double clamp_margin_db = 4.0;
    bool use_eight_connectivity = true;
    bool fill_all_components = false;

    QJsonObject toJson() const;
};

struct RasterPowerSuggestion {
    bool valid = false;
    double power_dbfs = 0.0;
    double confidence = 0.0;
    int source_count = 0;
    QString model;
};

struct RasterInpaintCell {
    std::size_t index = 0;
    double power_dbfs = 0.0;
    double confidence = 0.0;
    int layer = 0;
};

struct RasterInpaintingResult {
    std::vector<RasterInpaintCell> cells;
    int selected_missing_cells = 0;
    int remaining_missing_cells = 0;
    int completed_layers = 0;
    double minimum_applied_confidence = 0.0;
    double mean_applied_confidence = 0.0;
    QString warning;
};

class RasterInpainting {
  public:
    static bool validate(const RasterInpaintingSettings& settings, QString* error = nullptr);

    static RasterPowerSuggestion suggest(const RadioRasterGrid& grid,
                                         const std::vector<double>& powerDbfs,
                                         const std::vector<RasterCellState>& state,
                                         std::size_t targetIndex,
                                         const RasterInpaintingSettings& settings);

    static RasterInpaintingResult fill(const RadioRasterGrid& grid,
                                       const std::vector<double>& powerDbfs,
                                       const std::vector<RasterCellState>& state,
                                       std::size_t seedIndex,
                                       const RasterInpaintingSettings& settings);
};

}  // namespace b210post

#endif // __B210_RASTER_INPAINTING_HPP
