#ifndef __B210_RASTER_RENDERER_HPP
#define __B210_RASTER_RENDERER_HPP

#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QStringList>
#include <cstdint>
#include <vector>

#include "Types.hpp"

namespace b210post {

struct BuildingValidation {
  bool valid = false;
  QStringList errors;
  std::vector<std::vector<std::uint64_t>> loops;
};

struct RadioRasterGrid {
  int width_px = 0;
  int height_px = 0;
  double meters_per_pixel = 1.0;
  std::vector<double> power_dbfs;
  std::vector<unsigned> sample_count;
  std::vector<std::uint8_t> building_mask;

  std::size_t cellCount() const {
    return static_cast<std::size_t>(width_px) * height_px;
  }
};

struct RenderBundle {
  RasterFrame frame;
  QImage building;
  QImage tx;
  QImage radio;
  QImage count;
  QJsonObject metadata;
  QStringList warnings;
  unsigned max_sample_count = 0;
  RadioRasterGrid radio_grid;
  double default_power_dbfs = -120.0;
  double power_min_dbfs = -120.0;
  double power_max_dbfs = -20.0;
  RasterRenderOptions render_options;
};

class RasterRenderer {
 public:
  static BuildingValidation validateBuilding(const BuildingDataset& building);
  static RasterFrame buildFrame(const RasterRegion& region, QString* error = nullptr);
  static RenderBundle render(const RadioDataset& radio, const BuildingDataset& building,
                             const RasterFrame& frame, double defaultPower, double powerMin,
                             double powerMax, const RasterRenderOptions& options = {});
  static QPointF worldToRasterLocal(double east, double north, const RasterFrame& frame);
  static QPointF rasterLocalToWorld(double localX, double localY, const RasterFrame& frame);
  static QImage renderRadioGrid(const RadioRasterGrid& grid,
                                const std::vector<double>& displayedPowerDbfs, double defaultPower,
                                double powerMin, double powerMax, RasterColorMode colorMode);
  static QImage addColorbar(const QImage& raster, double minimum, double maximum,
                            const QString& title, RasterColorMode colorMode);
  static QString alternateRadioMapFileName(const QString& primaryFileName,
                                           RasterColorMode alternateMode);
  static bool saveMetadata(const QString& path, const QJsonObject& metadata,
                           QString* error = nullptr);
};

}  // namespace b210post

#endif // __B210_RASTER_RENDERER_HPP
