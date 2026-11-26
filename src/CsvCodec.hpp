#ifndef __B210_CSV_CODEC_HPP
#define __B210_CSV_CODEC_HPP
#include <QStringList>

#include "Types.hpp"

namespace b210post {

struct LoadReport {
  QStringList warnings;
  QStringList errors;
  int skipped_rows = 0;
  bool ok() const {
    return errors.isEmpty();
  }
};

struct RecoveryResult {
  RadioDataset dataset;
  LoadReport report;
  int raw_rf_rows = 0;
  int confirmed_points = 0;
  int rejected_rows = 0;
};

enum class BuildingFileKind { TypeB, RasterFrame, PrivateTypeB, PrivateRasterFrame };

struct BuildingSource {
  BuildingDataset building;
  BuildingFileKind kind = BuildingFileKind::TypeB;
  bool has_raster_frame = false;
  EnuReference project_reference;
  std::uint64_t origin_point_id = 0;
  RasterRegion region;
  double power_min_dbfs = -120.0;
  double power_max_dbfs = -20.0;
  double default_power_dbfs = -120.0;
};

class CsvCodec {
 public:
  static QStringList parseCsvLine(const QString& line, bool* ok = nullptr);
  static bool loadRadio(const QString& path, const EnuReference& fallback, RadioDataset& out,
                        LoadReport& report);
  static bool saveRadio(const QString& path, const RadioDataset& data, QString* error = nullptr);
  static bool savePrivateRadio(const QString& path, const RadioDataset& data,
                               const QString& localFrameId, QString* error = nullptr);
  static bool loadBuilding(const QString& path, BuildingDataset& out, LoadReport& report);
  static bool saveBuilding(const QString& path, const BuildingDataset& data,
                           QString* error = nullptr);
  static bool savePrivateBuilding(const QString& path, const BuildingDataset& data,
                                  const QString& localFrameId, QString* error = nullptr);
  static bool loadBuildingSource(const QString& path, BuildingSource& out, LoadReport& report);
  static bool saveRasterFrame(const QString& path, const BuildingDataset& data,
                              const EnuReference& projectReference, std::uint64_t originPointId,
                              const RasterRegion& region, double powerMin, double powerMax,
                              double defaultPower, QString* error = nullptr);
  static bool savePrivateRasterFrame(const QString& path, const BuildingDataset& data,
                                     const QString& localFrameId, std::uint64_t originPointId,
                                     const RasterRegion& region, double powerMin, double powerMax,
                                     double defaultPower, QString* error = nullptr);
  static bool createSyntheticWgs84Copies(const RadioDataset& privateRadio,
                                         const BuildingDataset& privateBuilding,
                                         const EnuReference& reference, RadioDataset& radioCopy,
                                         BuildingDataset& buildingCopy, QString* error = nullptr);
  static void alignRadio(RadioDataset& radio, const EnuReference& target);
  static bool rebaseRadioToFirstRx(RadioDataset& radio, EnuReference& reference,
                                   std::uint64_t& originPointId);
  static void alignBuilding(BuildingDataset& building, const EnuReference& target,
                            LoadReport* report = nullptr);
  static RecoveryResult recoverRawTimeline(const QString& path, const EnuReference& fallback);
};

}  // namespace b210post

#endif  // __B210_CSV_CODEC_HPP
