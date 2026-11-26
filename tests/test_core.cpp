#include <QFile>
#include <QGuiApplication>
#include <QLineF>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "BuildingShapes.hpp"
#include "CoordinateTransform.hpp"
#include "CsvCodec.hpp"
#include "Models.hpp"
#include "RasterCompletion.hpp"
#include "RasterInpainting.hpp"
#include "RasterRenderer.hpp"

using namespace b210post;

namespace {
int failures = 0;

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

RadioPoint makeRadioPoint(std::uint64_t id, double east, double north, double power,
                          const EnuReference& reference) {
  RadioPoint point;
  point.id = id;
  point.rf_sequence = id;
  point.east_m = east;
  point.north_m = north;
  point.up_m = 0.0;
  point.power_dbfs = power;
  const auto wgs = CoordinateTransform::enuToWgs84(east, north, 0.0, reference);
  point.latitude_deg = wgs.latitude_deg;
  point.longitude_deg = wgs.longitude_deg;
  point.altitude_m = wgs.altitude_m;
  point.geo_status = GeoStatus::MeasuredWgs84;
  return point;
}

SurveyPoint makeSurveyPoint(std::uint64_t id, double east, double north,
                            const EnuReference& reference) {
  SurveyPoint point;
  point.id = id;
  point.east_m = east;
  point.north_m = north;
  point.up_m = 0.0;
  const auto wgs = CoordinateTransform::enuToWgs84(east, north, 0.0, reference);
  point.latitude_deg = wgs.latitude_deg;
  point.longitude_deg = wgs.longitude_deg;
  point.altitude_m = wgs.altitude_m;
  point.geo_status = GeoStatus::MeasuredWgs84;
  return point;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QGuiApplication app(argc, argv);
  QTemporaryDir directory;
  check(directory.isValid(), "temporary directory");

  RectangleShapeSnapshot rectangleSnapshot;
  rectangleSnapshot.center_east_m = 10.0;
  rectangleSnapshot.center_north_m = 20.0;
  rectangleSnapshot.width_m = 4.0;
  rectangleSnapshot.height_m = 2.0;
  rectangleSnapshot.rotation_deg = 90.0;
  rectangleSnapshot.discretize_edge = {{true, true, false, false}};
  rectangleSnapshot.points_per_edge = {{5, 3, 2, 2}};
  const auto rectangleShape = createBuildingShape(rectangleSnapshot);
  const auto rectanglePoints = rectangleShape->discretize();
  check(rectanglePoints.size() == 8, "rectangle honors independent per-edge point counts");
  check(std::abs(rectanglePoints.front().x() - 11.0) < 1e-12 &&
            std::abs(rectanglePoints.front().y() - 18.0) < 1e-12,
        "rectangle rotates around its center");
  const auto rectangleCopy = rectangleShape->cloneAt({30.0, 40.0});
  check(rectangleCopy->center() == QPointF(30.0, 40.0) &&
            rectangleCopy->discretize().size() == rectanglePoints.size(),
        "shape copy preserves parameters and changes only its center");

  CircleShapeSnapshot circleSnapshot;
  circleSnapshot.center_east_m = -2.0;
  circleSnapshot.center_north_m = 3.0;
  circleSnapshot.radius_m = 7.0;
  circleSnapshot.circumference_points = 12;
  const auto circlePoints = createBuildingShape(circleSnapshot)->discretize();
  check(circlePoints.size() == 12, "circle honors circumference point count");
  check(std::all_of(circlePoints.begin(), circlePoints.end(),
                    [&](const QPointF& point) {
                      return std::abs(QLineF(point, QPointF(-2.0, 3.0)).length() - 7.0) < 1e-12;
                    }),
        "circle points are equally radial around its center");

  BuildingDataset generatedShapes;
  std::uint64_t generatedId = 0;
  const auto appendLoop = [&](const std::vector<QPointF>& positions) {
    std::vector<std::uint64_t> ids;
    for (const auto& position : positions) {
      SurveyPoint point;
      point.id = ++generatedId;
      point.east_m = position.x();
      point.north_m = position.y();
      point.up_m = 0.0;
      generatedShapes.points.push_back(point);
      ids.push_back(point.id);
    }
    for (std::size_t index = 0; index < ids.size(); ++index)
      generatedShapes.edges.push_back({ids[index], ids[(index + 1) % ids.size()]});
  };
  appendLoop(rectanglePoints);
  appendLoop(circlePoints);
  const auto generatedValidation = RasterRenderer::validateBuilding(generatedShapes);
  check(generatedValidation.valid && generatedValidation.loops.size() == 2,
        "discretized shapes form independent closed raster loops");

  EnuReference reference{35.0, 127.0, 50.0, true, ReferenceSource::File};
  const auto wgs = CoordinateTransform::enuToWgs84(100.0, 200.0, 5.0, reference);
  const auto enu = CoordinateTransform::wgs84ToEnu(wgs.latitude_deg, wgs.longitude_deg,
                                                   wgs.altitude_m, reference);
  check(std::abs(enu.east_m - 100.0) < 1e-4 && std::abs(enu.north_m - 200.0) < 1e-4,
        "ENU/WGS84 round trip");

  const QString missingReferencePath = directory.filePath("missing_reference.csv");
  {
    QFile file(missingReferencePath);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(
        "id,rf_sequence,measurement_segment_id,rf_timestamp_ns,latitude_deg,longitude_deg,"
        "altitude_m,east_m,north_m,up_m,power_dbfs,processing_mode,origin,valid,edited\n"
        "1,1,0,0,,,,10,20,0,-80,0,2,1,1\n");
  }
  RadioDataset rejected;
  LoadReport rejectedReport;
  check(!CsvCodec::loadRadio(missingReferencePath, reference, rejected, rejectedReport),
        "standard Type-A without ENU_REF is rejected");

  RadioDataset radio;
  radio.reference = reference;
  radio.points = {makeRadioPoint(1, 12.0, 34.0, -10.0, reference),
                  makeRadioPoint(2, 22.0, 54.0, -20.0, reference),
                  makeRadioPoint(3, -5.0, 7.0, 0.0, reference)};
  radio.points.back().role = RadioRole::Tx;
  radio.points.front().visible = false;

  QString error;
  const QString standardTypeAPath = directory.filePath("standard_type_a.csv");
  EnuReference unrelatedReference{-33.0, 18.0, 1000.0, true, ReferenceSource::SettingsFallback};
  check(CsvCodec::saveRadio(standardTypeAPath, radio, &error), "standard Type-A save");
  QFile standardTypeAFile(standardTypeAPath);
  check(standardTypeAFile.open(QIODevice::ReadOnly), "open standard Type-A compatibility file");
  const QByteArray standardTypeABytes = standardTypeAFile.readAll();
  standardTypeAFile.close();
  check(!standardTypeABytes.contains("generation_kind") &&
            !standardTypeABytes.contains("interpolation_batch_id"),
        "standard Type-A contains no post-only interpolation columns");
  RadioDataset standardReloaded;
  LoadReport standardReport;
  check(CsvCodec::loadRadio(standardTypeAPath, reference, standardReloaded, standardReport),
        "strict standard Type-A reload");
  check(standardReloaded.points.front().east_m == 0.0 &&
            standardReloaded.points.front().north_m == 0.0,
        "first valid RX point defines exact local ENU origin");
  check(std::abs(standardReloaded.points[1].east_m - 10.0) < 1e-3 &&
            std::abs(standardReloaded.points[1].north_m - 20.0) < 1e-3,
        "first-point rebase preserves local east/north shape");
  RadioDataset standardWithOtherSettings;
  LoadReport otherSettingsReport;
  check(CsvCodec::loadRadio(standardTypeAPath, unrelatedReference, standardWithOtherSettings,
                            otherSettingsReport),
        "standard Type-A reload with unrelated Settings origin");
  check(std::abs(standardWithOtherSettings.points[1].east_m - standardReloaded.points[1].east_m) <
                1e-9 &&
            std::abs(standardWithOtherSettings.points[1].north_m -
                     standardReloaded.points[1].north_m) < 1e-9,
        "Settings origin never changes standard Type-A display geometry");
  int rxCount = 0;
  int txCount = 0;
  for (const auto& point : standardReloaded.points)
    point.role == RadioRole::Tx ? ++txCount : ++rxCount;
  check(rxCount == 2 && txCount == 1, "RX/TX survives standard round trip");
  check(standardReloaded.points.front().visible,
        "session visibility is not persisted into standard Type-A");

  const QString privateTypeAPath = directory.filePath("private_type_a.csv");
  const QString frameId = QStringLiteral("unit-test-frame");
  standardReloaded.points.front().generation_kind = PointGenerationKind::Interpolated;
  standardReloaded.points.front().interpolation_batch_id = 7;
  standardReloaded.points.front().interpolation_method = QStringLiteral("idw");
  check(CsvCodec::savePrivateRadio(privateTypeAPath, standardReloaded, frameId, &error),
        "private Type-A save");
  RadioDataset privateA1;
  RadioDataset privateA2;
  LoadReport privateReport1;
  LoadReport privateReport2;
  check(CsvCodec::loadRadio(privateTypeAPath, reference, privateA1, privateReport1),
        "private Type-A load with first Settings origin");
  check(CsvCodec::loadRadio(privateTypeAPath, unrelatedReference, privateA2, privateReport2),
        "private Type-A load with unrelated Settings origin");
  check(privateA1.private_enu && privateA1.local_frame_id == frameId, "private Type-A metadata");
  check(privateA1.points.front().generation_kind == PointGenerationKind::Interpolated &&
            privateA1.points.front().interpolation_batch_id == 7 &&
            privateA1.points.front().interpolation_method == QStringLiteral("idw"),
        "private Type-A v2 preserves interpolation provenance");
  check(privateA1.points.size() == privateA2.points.size(), "private row count preserved");
  for (std::size_t i = 0; i < privateA1.points.size(); ++i) {
    check(privateA1.points[i].east_m == privateA2.points[i].east_m &&
              privateA1.points[i].north_m == privateA2.points[i].north_m &&
              privateA1.points[i].up_m == privateA2.points[i].up_m,
          "private ENU is independent of Settings origin");
    check(!std::isfinite(privateA1.points[i].latitude_deg) &&
              privateA1.points[i].geo_status == GeoStatus::PrivateEnu,
          "private WGS84 stays removed");
  }
  check(QFile::rename(privateTypeAPath, directory.filePath("private_type_a_moved.csv")),
        "input file is closed after load");

  BuildingDataset building;
  building.source_reference = reference;
  building.points = {
      makeSurveyPoint(1, 0.0, 0.0, reference), makeSurveyPoint(2, 10.0, 0.0, reference),
      makeSurveyPoint(3, 10.0, 10.0, reference), makeSurveyPoint(4, 0.0, 10.0, reference)};
  building.edges = {{1, 2}, {2, 3}, {3, 4}, {4, 1}};
  check(RasterRenderer::validateBuilding(building).valid, "closed Type-B validation");

  BuildingDataset privateBuilding = building;
  privateBuilding.private_enu = true;
  privateBuilding.local_frame_id = frameId;
  for (auto& point : privateBuilding.points) {
    point.latitude_deg = std::numeric_limits<double>::quiet_NaN();
    point.longitude_deg = std::numeric_limits<double>::quiet_NaN();
    point.altitude_m = std::numeric_limits<double>::quiet_NaN();
    point.geo_status = GeoStatus::PrivateEnu;
  }
  RadioDataset syntheticRadio;
  BuildingDataset syntheticBuilding;
  check(CsvCodec::createSyntheticWgs84Copies(privateA1, privateBuilding, unrelatedReference,
                                             syntheticRadio, syntheticBuilding, &error),
        "Private Type-A/B synthesize WGS84 copies");
  check(!syntheticRadio.private_enu && !syntheticBuilding.private_enu &&
            syntheticRadio.reference.source == ReferenceSource::SettingsFallback &&
            syntheticBuilding.source_reference.source == ReferenceSource::SettingsFallback,
        "synthetic copies become standard datasets with a synthetic reference");
  check(syntheticRadio.points.size() == privateA1.points.size() &&
            syntheticBuilding.points.size() == privateBuilding.points.size(),
        "synthetic conversion preserves point counts");
  for (std::size_t index = 0; index < privateA1.points.size(); ++index) {
    const auto& sourcePoint = privateA1.points[index];
    const auto& convertedPoint = syntheticRadio.points[index];
    const auto convertedEnu =
        CoordinateTransform::wgs84ToEnu(convertedPoint.latitude_deg, convertedPoint.longitude_deg,
                                        convertedPoint.altitude_m, unrelatedReference);
    check(convertedPoint.east_m == sourcePoint.east_m &&
              convertedPoint.north_m == sourcePoint.north_m &&
              convertedPoint.up_m == sourcePoint.up_m &&
              convertedPoint.geo_status == GeoStatus::SyntheticWgs84,
          "synthetic Type-A conversion leaves authoritative ENU unchanged");
    check(std::abs(convertedEnu.east_m - sourcePoint.east_m) < 1e-4 &&
              std::abs(convertedEnu.north_m - sourcePoint.north_m) < 1e-4 &&
              std::abs(convertedEnu.up_m - sourcePoint.up_m) < 1e-4,
          "synthetic Type-A WGS84 is consistent with private ENU");
  }
  BuildingDataset mismatchedPrivateBuilding = privateBuilding;
  mismatchedPrivateBuilding.local_frame_id = QStringLiteral("different-frame");
  check(!CsvCodec::createSyntheticWgs84Copies(privateA1, mismatchedPrivateBuilding,
                                              unrelatedReference, syntheticRadio, syntheticBuilding,
                                              &error),
        "synthetic conversion rejects mismatched private frames");

  const QString syntheticTypeAPath = directory.filePath("synthetic_type_a.csv");
  const QString syntheticTypeBPath = directory.filePath("synthetic_type_b.csv");
  check(CsvCodec::saveRadio(syntheticTypeAPath, syntheticRadio, &error),
        "synthetic standard Type-A save");
  check(CsvCodec::saveBuilding(syntheticTypeBPath, syntheticBuilding, &error),
        "synthetic standard Type-B save");
  RadioDataset reloadedSyntheticRadio;
  BuildingDataset reloadedSyntheticBuilding;
  LoadReport reloadedSyntheticRadioReport;
  LoadReport reloadedSyntheticBuildingReport;
  check(CsvCodec::loadRadio(syntheticTypeAPath, reference, reloadedSyntheticRadio,
                            reloadedSyntheticRadioReport),
        "synthetic Type-A reloads as strict standard data");
  check(CsvCodec::loadBuilding(syntheticTypeBPath, reloadedSyntheticBuilding,
                               reloadedSyntheticBuildingReport),
        "synthetic Type-B reloads as strict standard data");

  const QString standardTypeBPath = directory.filePath("standard_type_b.csv");
  check(CsvCodec::saveBuilding(standardTypeBPath, building, &error), "standard Type-B save");
  QFile standardTypeBFile(standardTypeBPath);
  standardTypeBFile.open(QIODevice::ReadOnly);
  const QByteArray standardTypeBBytes = standardTypeBFile.readAll();
  standardTypeBFile.close();
  check(!standardTypeBBytes.contains("#B210POST"),
        "standard Type-B contains no post-processor records");

  const QString privateFramePath = directory.filePath("private_frame.csv");
  RasterRegion region{5.0, 25.0, 4.0, 16.0, 1.0, 17.5};
  const QString syntheticFramePath = directory.filePath("synthetic_frame.csv");
  check(CsvCodec::saveRasterFrame(syntheticFramePath, syntheticBuilding, unrelatedReference, 1,
                                  region, -120.0, -20.0, -120.0, &error),
        "synthetic standard Raster Frame save");
  BuildingSource syntheticFrameSource;
  LoadReport syntheticFrameReport;
  check(CsvCodec::loadBuildingSource(syntheticFramePath, syntheticFrameSource,
                                     syntheticFrameReport) &&
            syntheticFrameSource.kind == BuildingFileKind::RasterFrame &&
            syntheticFrameSource.has_raster_frame,
        "synthetic Raster Frame reloads as strict standard data");
  check(CsvCodec::savePrivateRasterFrame(privateFramePath, building, frameId, 1, region, -120.0,
                                         -20.0, -120.0, &error),
        "private Raster Frame save");
  BuildingSource source;
  LoadReport sourceReport;
  check(CsvCodec::loadBuildingSource(privateFramePath, source, sourceReport),
        "private Raster Frame load");
  check(source.kind == BuildingFileKind::PrivateRasterFrame && source.has_raster_frame &&
            source.building.private_enu && source.building.local_frame_id == frameId,
        "private Raster Frame detection and metadata");
  check(source.building.points[2].east_m == 10.0 && source.building.points[2].north_m == 10.0,
        "private building geometry preserved exactly");
  check(std::abs(source.region.rotation_deg - 17.5) < 1e-12,
        "Raster Frame rotation survives round trip");

  QFile currentFrame(privateFramePath);
  check(currentFrame.open(QIODevice::ReadOnly), "open current Raster Frame for compatibility test");
  QByteArray legacyFrameBytes = currentFrame.readAll();
  currentFrame.close();
  legacyFrameBytes.replace("#B210POST_PRIVATE_RASTER_FRAME,2", "#B210POST_PRIVATE_RASTER_FRAME,1");
  const int rotationStart = legacyFrameBytes.indexOf("#RASTER_ROTATION_DEG,");
  if (rotationStart >= 0) {
    const int rotationEnd = legacyFrameBytes.indexOf('\n', rotationStart);
    legacyFrameBytes.remove(rotationStart, rotationEnd - rotationStart + 1);
  }
  const QString legacyFramePath = directory.filePath("legacy_private_frame.csv");
  QFile legacyFrame(legacyFramePath);
  check(legacyFrame.open(QIODevice::WriteOnly), "create legacy Raster Frame fixture");
  legacyFrame.write(legacyFrameBytes);
  legacyFrame.close();
  BuildingSource legacySource;
  LoadReport legacyReport;
  check(CsvCodec::loadBuildingSource(legacyFramePath, legacySource, legacyReport) &&
            legacySource.region.rotation_deg == 0.0,
        "Raster Frame v1 remains load-compatible with zero rotation");

  RasterRegion smallRegion{1.0, 1.0, 1.0, 1.0, 1.0};
  QString frameError;
  const auto smallFrame = RasterRenderer::buildFrame(smallRegion, &frameError);
  check(frameError.isEmpty() && smallFrame.width_px == 2 && smallFrame.height_px == 2,
        "fixed raster frame dimensions");
  RadioDataset averagingRadio;
  averagingRadio.reference = reference;
  averagingRadio.points = {makeRadioPoint(1, 0.0, 0.0, -10.0, reference),
                           makeRadioPoint(2, 0.0, 0.0, -20.0, reference),
                           makeRadioPoint(3, 0.0, 0.0, 0.0, reference)};
  averagingRadio.points.back().role = RadioRole::Tx;
  averagingRadio.points.front().visible = false;
  BuildingDataset emptyBuilding;
  const auto rendered =
      RasterRenderer::render(averagingRadio, emptyBuilding, smallFrame, -100.0, -100.0, 0.0);
  const double expected = 10.0 * std::log10((0.1 + 0.01) / 2.0);
  const int gray = rendered.radio.constScanLine(1)[1];
  const int wanted = int(std::lround(255.0 * (expected + 100.0) / 100.0));
  check(std::abs(gray - wanted) <= 1,
        "linear-domain RX mean is unchanged by interactive visibility");
  check(rendered.tx.constScanLine(1)[1] == 255, "TX role produces TX mask");
  check(rendered.radio_grid.sample_count[3] == 2 &&
            std::isfinite(rendered.radio_grid.power_dbfs[3]) &&
            !std::isfinite(rendered.radio_grid.power_dbfs[0]),
        "radio raster preserves measured and truly missing cell state before color mapping");
  const auto pairedGray =
      RasterRenderer::renderRadioGrid(rendered.radio_grid, rendered.radio_grid.power_dbfs, -100.0,
                                      -100.0, 0.0, RasterColorMode::Grayscale);
  const auto pairedRgb =
      RasterRenderer::renderRadioGrid(rendered.radio_grid, rendered.radio_grid.power_dbfs, -100.0,
                                      -100.0, 0.0, RasterColorMode::Color);
  check(pairedGray.size() == pairedRgb.size() && pairedGray.format() == QImage::Format_Grayscale8 &&
            pairedRgb.format() == QImage::Format_ARGB32,
        "paired Radio Map modes render the same final scalar grid at identical dimensions");
  check(RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey_rgb.png"),
                                                  RasterColorMode::Grayscale) ==
                QStringLiteral("survey_gray.png") &&
            RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey_RGB.PNG"),
                                                      RasterColorMode::Grayscale) ==
                QStringLiteral("survey_gray.PNG") &&
            RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey.png"),
                                                      RasterColorMode::Grayscale) ==
                QStringLiteral("survey_gray.png"),
        "Gray companion naming replaces RGB case-insensitively before adding a suffix");
  check(RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey_gray.png"),
                                                  RasterColorMode::Color) ==
                QStringLiteral("survey_rgb.png") &&
            RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey_GRAY.PNG"),
                                                      RasterColorMode::Color) ==
                QStringLiteral("survey_rgb.PNG") &&
            RasterRenderer::alternateRadioMapFileName(QStringLiteral("survey.png"),
                                                      RasterColorMode::Color) ==
                QStringLiteral("survey_rgb.png"),
        "RGB companion naming replaces Gray case-insensitively before adding a suffix");

  RadioRasterGrid completionGrid = rendered.radio_grid;
  completionGrid.building_mask[0] = 1;
  RasterCompletionSettings completionSettings;
  completionSettings.neighbor_count = 2;
  completionSettings.minimum_neighbors = 1;
  completionSettings.minimum_quadrants = 1;
  completionSettings.maximum_distance_m = 0.0;
  const auto completion = RasterCompletion::complete(completionGrid, completionSettings);
  check(completion.state[0] == RasterCellState::Building &&
            completion.state[3] == RasterCellState::MeasuredAggregate &&
            completion.state[1] == RasterCellState::RasterInterpolated,
        "raster completion fills only sample_count-zero non-building cells");
  check(completionGrid.sample_count[1] == 0 && completionGrid.sample_count[3] == 2,
        "raster completion never changes physical sample counts");
  check(completion.power_dbfs[3] == rendered.radio_grid.power_dbfs[3],
        "raster completion preserves measured aggregate power exactly");

  RadioRasterGrid inpaintGrid;
  inpaintGrid.width_px = 5;
  inpaintGrid.height_px = 5;
  inpaintGrid.meters_per_pixel = 1.0;
  inpaintGrid.power_dbfs.assign(25, std::numeric_limits<double>::quiet_NaN());
  inpaintGrid.sample_count.assign(25, 0);
  inpaintGrid.building_mask.assign(25, 0);
  std::vector<RasterCellState> inpaintState(25, RasterCellState::Missing);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      if (x == 0 || x == 4 || y == 0 || y == 4) {
        const auto index = static_cast<std::size_t>(y) * 5 + x;
        inpaintGrid.power_dbfs[index] = -60.0 + 2.0 * x - y;
        inpaintGrid.sample_count[index] = 1;
        inpaintState[index] = RasterCellState::MeasuredAggregate;
      }
    }
  }
  RasterInpaintingSettings inpaintSettings;
  inpaintSettings.neighborhood_radius_px = 4;
  inpaintSettings.minimum_sources = 3;
  inpaintSettings.minimum_confidence = 0.05;
  const auto suggestion = RasterInpainting::suggest(inpaintGrid, inpaintGrid.power_dbfs,
                                                    inpaintState, 12, inpaintSettings);
  check(suggestion.valid && std::abs(suggestion.power_dbfs - (-58.0)) < 1e-6,
        "power suggestion recovers a local directional plane");
  const auto inpaint = RasterInpainting::fill(inpaintGrid, inpaintGrid.power_dbfs, inpaintState, 12,
                                              inpaintSettings);
  check(inpaint.cells.size() == 9 && inpaint.remaining_missing_cells == 0 &&
            inpaint.completed_layers >= 2,
        "progressive fill advances synchronously from the boundary to the center");
  check(inpaintGrid.sample_count[12] == 0 && inpaintState[12] == RasterCellState::Missing,
        "progressive fill does not mutate source raster state or sample counts");
  auto maskedGrid = inpaintGrid;
  auto maskedState = inpaintState;
  maskedGrid.building_mask[12] = 1;
  maskedState[12] = RasterCellState::Building;
  const auto maskedInpaint =
      RasterInpainting::fill(maskedGrid, maskedGrid.power_dbfs, maskedState, 6, inpaintSettings);
  check(std::none_of(maskedInpaint.cells.begin(), maskedInpaint.cells.end(),
                     [](const RasterInpaintCell& cell) {
                       return cell.index == 12;
                     }),
        "progressive fill never proposes a building cell");

  RasterRenderOptions smoothGrayOptions;
  smoothGrayOptions.smooth_export = true;
  smoothGrayOptions.supersampling_factor = 4;
  const auto smoothGray = RasterRenderer::render(averagingRadio, emptyBuilding, smallFrame, -100.0,
                                                 -100.0, 0.0, smoothGrayOptions);
  check(smoothGray.radio == rendered.radio && smoothGray.count == rendered.count,
        "smooth export never blurs or fills power/count scalar cells");

  RasterRegion rotatedRegion{1.1, 2.1, 3.1, 4.1, 0.5, 90.0};
  const auto rotatedFrame = RasterRenderer::buildFrame(rotatedRegion, &frameError);
  check(frameError.isEmpty() && rotatedFrame.width_px == 8 && rotatedFrame.height_px == 16,
        "rotated frame keeps outward-rounded local dimensions");
  const auto worldPoint = RasterRenderer::rasterLocalToWorld(0.5, 1.0, rotatedFrame);
  const auto localPoint =
      RasterRenderer::worldToRasterLocal(worldPoint.x(), worldPoint.y(), rotatedFrame);
  check(std::abs(localPoint.x() - 0.5) < 1e-12 && std::abs(localPoint.y() - 1.0) < 1e-12,
        "rotated raster coordinate round trip");

  RasterRenderOptions presentationOptions;
  presentationOptions.color_mode = RasterColorMode::Color;
  presentationOptions.smooth_export = true;
  presentationOptions.supersampling_factor = 4;
  const auto presentation = RasterRenderer::render(averagingRadio, emptyBuilding, smallFrame,
                                                   -100.0, -100.0, 0.0, presentationOptions);
  check(presentation.radio.size() == rendered.radio.size() &&
            presentation.building.size() == rendered.building.size(),
        "smooth color output preserves exact raster dimensions");
  check(presentation.radio.format() == QImage::Format_ARGB32, "color mode produces a color image");
  const auto withColorbar = RasterRenderer::addColorbar(
      presentation.radio, -100.0, 0.0, QStringLiteral("Power [dBFS]"), RasterColorMode::Color);
  check(withColorbar.width() > presentation.radio.width() &&
            withColorbar.height() >= presentation.radio.height(),
        "colorbar is a separate presentation canvas");

  RadioTableModel radioModel;
  radioModel.setDataset(&averagingRadio);
  check(radioModel.columnCount() == 21 &&
            radioModel.data(radioModel.index(0, 1), Qt::CheckStateRole).toInt() == Qt::Unchecked,
        "Type-A model exposes the Visible checkbox column");
  check(radioModel.setData(radioModel.index(0, 1), Qt::Checked, Qt::CheckStateRole) &&
            averagingRadio.points.front().visible,
        "Type-A Visible checkbox updates only in-memory view state");
  const double preciseLatitude = averagingRadio.points.front().latitude_deg;
  bool preciseLatitudeOk = false;
  const double editLatitude =
      radioModel.data(radioModel.index(0, 6), Qt::EditRole).toString().toDouble(&preciseLatitudeOk);
  check(preciseLatitudeOk && editLatitude == preciseLatitude,
        "Type-A EditRole preserves full double coordinate precision");
  BuildingTableModel buildingModel;
  buildingModel.setDataSource(&building, &reference);
  check(
      buildingModel.columnCount() == 13 &&
          buildingModel.data(buildingModel.index(0, 1), Qt::CheckStateRole).toInt() == Qt::Checked,
      "Type-B model exposes the Visible checkbox column");

  std::cout << (failures ? "FAILED" : "PASS") << '\n';
  return failures ? 1 : 0;
}
