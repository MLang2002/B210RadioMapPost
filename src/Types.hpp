#ifndef __B210_TYPES_HPP
#define __B210_TYPES_HPP

#include <QString>
#include <cstdint>
#include <limits>
#include <vector>

namespace b210post {

enum class ProcessingMode { Normal = 0, Lfm = 1, SpecGnu = 2, SpecGnuLfm = 3 };
enum class DataOrigin { Measured = 0, Imported = 1, Manual = 2 };
enum class PointGenerationKind { Original = 0, Manual = 1, Interpolated = 2 };
enum class RadioRole { Rx = 0, Tx = 1 };
enum class RasterColorMode { Grayscale = 0, Color = 1 };
enum class RasterResamplingFilter { Bilinear = 0, Bicubic = 1 };
enum class GeoStatus {
  MeasuredWgs84,
  DerivedFromFileReference,
  SyntheticWgs84,
  PrivateEnu,
  Unavailable
};
enum class ReferenceSource { File, SettingsFallback };

struct EnuReference {
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double altitude_m = 0.0;
  bool valid = false;
  ReferenceSource source = ReferenceSource::SettingsFallback;
};

struct EnuCoordinate {
  double east_m = 0.0, north_m = 0.0, up_m = 0.0;
};
struct Wgs84Coordinate {
  double latitude_deg = 0.0, longitude_deg = 0.0, altitude_m = 0.0;
};

struct RadioPoint {
  std::uint64_t id = 0;
  std::uint64_t rf_sequence = 0;
  std::uint64_t segment_id = 0;
  std::int64_t timestamp_ns = 0;
  double latitude_deg = std::numeric_limits<double>::quiet_NaN();
  double longitude_deg = std::numeric_limits<double>::quiet_NaN();
  double altitude_m = std::numeric_limits<double>::quiet_NaN();
  double east_m = std::numeric_limits<double>::quiet_NaN();
  double north_m = std::numeric_limits<double>::quiet_NaN();
  double up_m = std::numeric_limits<double>::quiet_NaN();
  double power_dbfs = std::numeric_limits<double>::quiet_NaN();
  RadioRole role = RadioRole::Rx;
  ProcessingMode processing_mode = ProcessingMode::Normal;
  DataOrigin origin = DataOrigin::Imported;
  bool valid = true;
  bool edited = false;
  bool visible = true;
  GeoStatus geo_status = GeoStatus::Unavailable;
  PointGenerationKind generation_kind = PointGenerationKind::Original;
  std::uint64_t interpolation_batch_id = 0;
  QString interpolation_method;
};

struct SurveyPoint {
  std::uint64_t id = 0;
  std::int64_t timestamp_ns = 0;
  double latitude_deg = std::numeric_limits<double>::quiet_NaN();
  double longitude_deg = std::numeric_limits<double>::quiet_NaN();
  double altitude_m = std::numeric_limits<double>::quiet_NaN();
  double east_m = std::numeric_limits<double>::quiet_NaN();
  double north_m = std::numeric_limits<double>::quiet_NaN();
  double up_m = std::numeric_limits<double>::quiet_NaN();
  int fix_quality = 0;
  DataOrigin origin = DataOrigin::Imported;
  bool edited = false;
  bool visible = true;
  GeoStatus geo_status = GeoStatus::Unavailable;
};

struct BuildingEdge {
  std::uint64_t a = 0, b = 0;
};

struct RadioDataset {
  EnuReference reference;
  std::vector<RadioPoint> points;
  bool reference_was_synthetic = false;
  bool private_enu = false;
  QString local_frame_id;
};

struct RasterRegion {
  double left_m = 50.0;
  double right_m = 50.0;
  double bottom_m = 50.0;
  double top_m = 50.0;
  double meters_per_pixel = 1.0;
  double rotation_deg = 0.0;
};

struct BuildingDataset {
  EnuReference source_reference;
  std::vector<SurveyPoint> points;
  std::vector<BuildingEdge> edges;
  bool private_enu = false;
  QString local_frame_id;
};

struct RasterFrame {
  double min_east_m = 0.0;
  double min_north_m = 0.0;
  double max_east_m = 0.0;
  double max_north_m = 0.0;
  double meters_per_pixel = 1.0;
  int width_px = 1;
  int height_px = 1;
  double rotation_deg = 0.0;
};

struct RasterRenderOptions {
  RasterColorMode color_mode = RasterColorMode::Grayscale;
  bool add_colorbar = false;
  bool smooth_export = false;
  int supersampling_factor = 4;
  RasterResamplingFilter resampling_filter = RasterResamplingFilter::Bicubic;
};

struct AppSettings {
  double fallback_latitude_deg = 0.0;
  double fallback_longitude_deg = 0.0;
  double fallback_altitude_m = 0.0;
  bool automatic_default_power = true;
  double manual_default_power_dbfs = -120.0;
  double power_min_dbfs = -120.0;
  double power_max_dbfs = -20.0;
  double meters_per_pixel = 1.0;
  double raster_left_m = 50.0;
  double raster_right_m = 50.0;
  double raster_bottom_m = 50.0;
  double raster_top_m = 50.0;
  double raster_rotation_deg = 0.0;
  RasterRenderOptions render_options;
  int point_size_px = 6;
  double inactive_opacity = 0.22;
};

inline QString geoStatusName(GeoStatus s) {
  switch (s) {
    case GeoStatus::MeasuredWgs84:
      return QStringLiteral("Measured WGS84");
    case GeoStatus::DerivedFromFileReference:
      return QStringLiteral("Derived WGS84");
    case GeoStatus::SyntheticWgs84:
      return QStringLiteral("Synthetic WGS84");
    case GeoStatus::PrivateEnu:
      return QStringLiteral("Private ENU");
    case GeoStatus::Unavailable:
      return QStringLiteral("Unavailable");
  }
  return {};
}

inline QString processingModeName(ProcessingMode m) {
  switch (m) {
    case ProcessingMode::Normal:
      return QStringLiteral("Normal");
    case ProcessingMode::Lfm:
      return QStringLiteral("LFM");
    case ProcessingMode::SpecGnu:
      return QStringLiteral("SpecGNU");
    case ProcessingMode::SpecGnuLfm:
      return QStringLiteral("SpecGNU+LFM");
  }
  return QStringLiteral("Normal");
}

inline QString radioRoleName(RadioRole role) {
  return role == RadioRole::Tx ? QStringLiteral("TX") : QStringLiteral("RX");
}

}  // namespace b210post

#endif // __B210_TYPES_HPP
