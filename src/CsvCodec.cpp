#include "CsvCodec.hpp"

#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "CoordinateTransform.hpp"

namespace b210post {
namespace {

constexpr double kHorizontalConsistencyToleranceM = 0.25;
constexpr double kVerticalConsistencyToleranceM = 0.50;

struct PrivateHeader {
  bool is_private = false;
  QString type;
  QString local_frame_id;
  bool enu_authority = false;
  bool geography_removed = false;
  bool axis_valid = false;
  bool unit_valid = false;
};

void configureNumericOutput(QTextStream* stream) {
  stream->setRealNumberNotation(QTextStream::SmartNotation);
  stream->setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
}

double number(const QString& s, bool* ok = nullptr) {
  bool good = false;
  double v = s.trimmed().toDouble(&good);
  if (ok)
    *ok = good;
  return good ? v : std::numeric_limits<double>::quiet_NaN();
}

qulonglong u64(const QString& s, bool* ok = nullptr) {
  bool good = false;
  auto v = s.trimmed().toULongLong(&good);
  if (ok)
    *ok = good;
  return good ? v : 0;
}

qlonglong i64(const QString& s, bool* ok = nullptr) {
  bool good = false;
  auto v = s.trimmed().toLongLong(&good);
  if (ok)
    *ok = good;
  return good ? v : 0;
}

bool finiteGeo(double la, double lo) {
  return std::isfinite(la) && std::isfinite(lo) && la >= -90 && la <= 90 && lo >= -180 && lo <= 180;
}

bool finiteEnu(double e, double n) {
  return std::isfinite(e) && std::isfinite(n);
}

bool finiteEnu(double e, double n, double u) {
  return finiteEnu(e, n) && std::isfinite(u);
}

QString field(const QStringList& r, const QHash<QString, int>& h, const QString& n) {
  auto it = h.find(n);
  return it == h.end() || it.value() >= r.size() ? QString() : r[it.value()];
}

QHash<QString, int> header(const QStringList& f) {
  QHash<QString, int> h;
  for (int i = 0; i < f.size(); ++i)
    h.insert(f[i].trimmed(), i);
  return h;
}

void writeNum(QTextStream& o, double v) {
  if (std::isfinite(v))
    o << v;
}

void writePrivateHeader(QTextStream& out, const QString& type, const QString& frameId) {
  const int version = type == QStringLiteral("RASTER_FRAME") || type == QStringLiteral("A") ? 2 : 1;
  out << "#B210POST_PRIVATE_" << type << ',' << version << '\n';
  out << "#LOCAL_FRAME_ID," << frameId << '\n';
  out << "#COORDINATE_AUTHORITY,ENU\n";
  out << "#GEOGRAPHIC_FIELDS,REMOVED\n";
  out << "#AXIS,EAST,NORTH,UP\n";
  out << "#UNIT,METER\n";
}

PrivateHeader inspectPrivateHeader(QFile& file, LoadReport& report) {
  PrivateHeader result;
  QTextStream input(&file);
  while (!input.atEnd()) {
    const auto row = CsvCodec::parseCsvLine(input.readLine());
    if (row.isEmpty())
      continue;
    if (row[0].startsWith("#B210POST_PRIVATE_")) {
      result.is_private = true;
      result.type = row[0].mid(QStringLiteral("#B210POST_PRIVATE_").size());
      const int version = row.value(1).toInt();
      const bool supported =
          version == 1 ||
          ((result.type == QStringLiteral("RASTER_FRAME") || result.type == QStringLiteral("A")) &&
           version == 2);
      if (!supported)
        report.errors << QStringLiteral("Unsupported private CSV version.");
    } else if (row[0] == QStringLiteral("#LOCAL_FRAME_ID")) {
      result.local_frame_id = row.value(1).trimmed();
    } else if (row[0] == QStringLiteral("#COORDINATE_AUTHORITY")) {
      result.enu_authority = row.value(1) == QStringLiteral("ENU");
    } else if (row[0] == QStringLiteral("#GEOGRAPHIC_FIELDS")) {
      result.geography_removed = row.value(1) == QStringLiteral("REMOVED");
    } else if (row[0] == QStringLiteral("#AXIS")) {
      result.axis_valid = row.value(1) == QStringLiteral("EAST") &&
                          row.value(2) == QStringLiteral("NORTH") &&
                          row.value(3) == QStringLiteral("UP");
    } else if (row[0] == QStringLiteral("#UNIT")) {
      result.unit_valid = row.value(1) == QStringLiteral("METER");
    }
  }
  file.seek(0);
  if (result.is_private && result.local_frame_id.isEmpty())
    report.errors << QStringLiteral("Private CSV is missing #LOCAL_FRAME_ID.");
  if (result.is_private && (!result.enu_authority || !result.geography_removed ||
                            !result.axis_valid || !result.unit_valid)) {
    report.errors << QStringLiteral(
        "Private CSV has missing/invalid ENU authority, geographic-removal, axis, or unit "
        "metadata.");
  }
  return result;
}

bool checkCoordinateConsistency(double latitude, double longitude, double altitude, double east,
                                double north, double up, const EnuReference& reference) {
  const auto expected = CoordinateTransform::wgs84ToEnu(latitude, longitude, altitude, reference);
  return std::hypot(expected.east_m - east, expected.north_m - north) <=
             kHorizontalConsistencyToleranceM &&
         std::abs(expected.up_m - up) <= kVerticalConsistencyToleranceM;
}

ProcessingMode modeFrom(const QString& s) {
  bool ok = false;
  int v = s.toInt(&ok);
  if (ok && v >= 0 && v <= 3)
    return static_cast<ProcessingMode>(v);
  if (s.compare("LFM", Qt::CaseInsensitive) == 0)
    return ProcessingMode::Lfm;
  if (s.compare("SpecGNU", Qt::CaseInsensitive) == 0)
    return ProcessingMode::SpecGnu;
  if (s.contains("SpecGNU", Qt::CaseInsensitive) && s.contains("LFM", Qt::CaseInsensitive))
    return ProcessingMode::SpecGnuLfm;
  return ProcessingMode::Normal;
}

RadioRole roleFrom(const QString& s) {
  return s.trimmed().compare("TX", Qt::CaseInsensitive) == 0 ? RadioRole::Tx : RadioRole::Rx;
}

struct RawGnss {
  qint64 t = 0;
  double la = 0, lo = 0, al = 0;
  bool valid = false;
  qulonglong segment = 0;
};

struct RawRf {
  qulonglong seq = 0, segment = 0;
  qint64 t = 0;
  double power = 0;
  ProcessingMode mode = ProcessingMode::Normal;
  bool valid = false;
  QString status;
};

}  // namespace

QStringList CsvCodec::parseCsvLine(const QString& line, bool* ok) {
  QStringList out;
  QString cur;
  bool quoted = false;
  for (int i = 0; i < line.size(); ++i) {
    QChar c = line[i];
    if (c == '\"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '\"') {
        cur += '\"';
        ++i;
      } else
        quoted = !quoted;
    } else if (c == ',' && !quoted) {
      out << cur;
      cur.clear();
    } else
      cur += c;
  }
  out << cur;
  if (ok)
    *ok = !quoted;
  return out;
}

bool CsvCodec::loadRadio(const QString& path, const EnuReference& fallback, RadioDataset& out,
                         LoadReport& report) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    report.errors << f.errorString();
    return false;
  }
  const PrivateHeader privateHeader = inspectPrivateHeader(f, report);
  if (privateHeader.is_private && privateHeader.type != QStringLiteral("A"))
    report.errors << QStringLiteral("The selected private CSV is not Type-A.");
  if (!report.ok()) {
    f.close();
    return false;
  }

  RadioDataset d;
  d.private_enu = privateHeader.is_private;
  d.local_frame_id = privateHeader.local_frame_id;
  if (d.private_enu) {
    d.reference = fallback;
    d.reference.valid = true;
    d.reference.source = ReferenceSource::SettingsFallback;
    d.reference_was_synthetic = true;
  }
  QTextStream in(&f);
  QHash<QString, int> h;
  std::vector<RadioPoint> txPoints;
  bool haveHeader = false;
  bool hasSegment = false;
  while (!in.atEnd()) {
    QString line = in.readLine();
    if (line.trimmed().isEmpty())
      continue;
    bool csvok = false;
    auto r = parseCsvLine(line, &csvok);
    if (!csvok) {
      report.errors << QStringLiteral("Unclosed CSV quote.");
      continue;
    }
    if (r[0] == "#ENU_REF" && r.size() >= 4) {
      if (d.private_enu) {
        report.errors << QStringLiteral("Private Type-A must not contain #ENU_REF.");
        continue;
      }
      bool a, b, c;
      double la = number(r[1], &a), lo = number(r[2], &b), al = number(r[3], &c);
      if (a && b && c && finiteGeo(la, lo)) {
        d.reference = {la, lo, al, true, ReferenceSource::File};
      } else {
        report.errors << QStringLiteral("Invalid #ENU_REF in standard Type-A CSV.");
      }
      continue;
    }
    if (r[0] == "#TX") {
      if (r.size() < 10) {
        ++report.skipped_rows;
        continue;
      }
      RadioPoint p;
      const int o = r.size() >= 11 ? 1 : 0;
      p.role = RadioRole::Tx;
      p.id = o ? u64(r[1]) : 0;
      p.timestamp_ns = i64(r[1 + o]);
      p.latitude_deg = number(r[2 + o]);
      p.longitude_deg = number(r[3 + o]);
      p.altitude_m = number(r[4 + o]);
      p.east_m = number(r[5 + o]);
      p.north_m = number(r[6 + o]);
      p.up_m = number(r[7 + o]);
      p.power_dbfs = number(r[8 + o]);
      p.valid = r[9 + o].toInt() != 0;
      p.rf_sequence = u64(r.value(10 + o));
      p.segment_id = u64(r.value(11 + o));
      p.processing_mode = modeFrom(r.value(12 + o));
      bool originOk = false;
      const int originValue = r.value(13 + o).toInt(&originOk);
      p.origin = originOk && originValue >= 0 && originValue <= 2
                     ? static_cast<DataOrigin>(originValue)
                     : DataOrigin::Imported;
      p.edited = r.value(14 + o).toInt() != 0;
      txPoints.push_back(p);
      continue;
    }
    if (r[0].trimmed() == "id") {
      h = header(r);
      haveHeader = true;
      hasSegment = h.contains("measurement_segment_id");
      continue;
    }
    if (r[0].startsWith('#'))
      continue;
    if (!haveHeader) {
      report.errors << QStringLiteral("Type-A data header is missing.");
      break;
    }
    RadioPoint p;
    p.id = u64(field(r, h, "id"));
    p.role = roleFrom(field(r, h, "role"));
    p.rf_sequence = u64(field(r, h, "rf_sequence"));
    p.segment_id = hasSegment ? u64(field(r, h, "measurement_segment_id")) : 0;
    p.timestamp_ns = i64(field(r, h, "rf_timestamp_ns"));
    p.latitude_deg = number(field(r, h, "latitude_deg"));
    p.longitude_deg = number(field(r, h, "longitude_deg"));
    p.altitude_m = number(field(r, h, "altitude_m"));
    p.east_m = number(field(r, h, "east_m"));
    p.north_m = number(field(r, h, "north_m"));
    p.up_m = number(field(r, h, "up_m"));
    p.power_dbfs = number(field(r, h, "power_dbfs"));
    p.processing_mode = modeFrom(field(r, h, "processing_mode"));
    bool originOk = false;
    const int originValue = field(r, h, "origin").toInt(&originOk);
    p.origin = originOk && originValue >= 0 && originValue <= 2
                   ? static_cast<DataOrigin>(originValue)
                   : DataOrigin::Imported;
    p.valid = field(r, h, "valid").isEmpty() || field(r, h, "valid").toInt() != 0;
    p.edited = field(r, h, "edited").toInt() != 0;
    if (d.private_enu && h.contains(QStringLiteral("generation_kind"))) {
      bool generationOk = false;
      const int generation = field(r, h, "generation_kind").toInt(&generationOk);
      if (generationOk && generation >= 0 && generation <= 2) {
        p.generation_kind = static_cast<PointGenerationKind>(generation);
      }
      p.interpolation_batch_id = u64(field(r, h, "interpolation_batch_id"));
      p.interpolation_method = field(r, h, "interpolation_method");
    }
    d.points.push_back(p);
  }
  d.points.insert(d.points.end(), txPoints.begin(), txPoints.end());
  f.close();
  if (!haveHeader)
    report.errors << QStringLiteral("Type-A data header is missing.");
  if (d.points.empty())
    report.errors << QStringLiteral("Type-A contains no power/GNSS points.");
  if (!d.private_enu && !d.reference.valid)
    report.errors << QStringLiteral(
        "Standard Type-A requires a valid #ENU_REF. Use Create Private Copy for anonymized data.");

  std::uint64_t nextId = 0;
  for (const auto& point : d.points)
    nextId = std::max(nextId, point.id);
  QSet<qulonglong> ids;
  for (auto& point : d.points) {
    if (point.id == 0 || ids.contains(qulonglong(point.id)))
      point.id = ++nextId;
    ids.insert(qulonglong(point.id));
    if (d.private_enu) {
      if (!finiteEnu(point.east_m, point.north_m, point.up_m)) {
        report.errors << QStringLiteral("Private Type-A row %1 has incomplete ENU coordinates.")
                             .arg(qulonglong(point.id));
        continue;
      }
      point.latitude_deg = std::numeric_limits<double>::quiet_NaN();
      point.longitude_deg = std::numeric_limits<double>::quiet_NaN();
      point.altitude_m = std::numeric_limits<double>::quiet_NaN();
      point.geo_status = GeoStatus::PrivateEnu;
      continue;
    }
    if (!finiteGeo(point.latitude_deg, point.longitude_deg) || !std::isfinite(point.altitude_m) ||
        !finiteEnu(point.east_m, point.north_m, point.up_m)) {
      report.errors << QStringLiteral(
                           "Standard Type-A row %1 requires complete WGS84 and ENU coordinates.")
                           .arg(qulonglong(point.id));
      continue;
    }
    if (!checkCoordinateConsistency(point.latitude_deg, point.longitude_deg, point.altitude_m,
                                    point.east_m, point.north_m, point.up_m, d.reference)) {
      report.errors << QStringLiteral(
                           "Type-A row %1 has inconsistent WGS84/ENU coordinates (>0.25 m "
                           "horizontal or >0.50 m vertical).")
                           .arg(qulonglong(point.id));
      continue;
    }
    point.geo_status = GeoStatus::MeasuredWgs84;
  }
  if (!report.ok())
    return false;
  if (!d.private_enu) {
    EnuReference firstPointReference;
    std::uint64_t firstPointId = 0;
    if (!rebaseRadioToFirstRx(d, firstPointReference, firstPointId)) {
      report.errors << QStringLiteral("Type-A has no valid RX point that can define local ENU.");
      return false;
    }
  }
  out = std::move(d);
  return true;
}

bool CsvCodec::saveRadio(const QString& path, const RadioDataset& d, QString* error) {
  if (d.private_enu) {
    if (error)
      *error = QStringLiteral(
          "Private ENU data cannot be exported as standard Type-A. Use Create Private Copy.");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(15);
  configureNumericOutput(&o);
  if (d.reference.valid)
    o << "#ENU_REF," << d.reference.latitude_deg << ',' << d.reference.longitude_deg << ','
      << d.reference.altitude_m << '\n';
  for (const auto& p : d.points) {
    if (p.role != RadioRole::Tx)
      continue;
    o << "#TX," << p.id << ',' << p.timestamp_ns << ',';
    writeNum(o, p.latitude_deg);
    o << ',';
    writeNum(o, p.longitude_deg);
    o << ',';
    writeNum(o, p.altitude_m);
    o << ',';
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',';
    writeNum(o, p.power_dbfs);
    o << ',' << (p.valid ? 1 : 0) << '\n';
  }
  o << "id,rf_sequence,measurement_segment_id,rf_timestamp_ns,latitude_deg,longitude_deg,"
       "altitude_m,east_m,north_m,up_m,power_dbfs,processing_mode,origin,valid,edited\n";
  for (const auto& p : d.points) {
    if (p.role == RadioRole::Tx)
      continue;
    o << p.id << ',' << p.rf_sequence << ',' << p.segment_id << ',' << p.timestamp_ns << ',';
    writeNum(o, p.latitude_deg);
    o << ',';
    writeNum(o, p.longitude_deg);
    o << ',';
    writeNum(o, p.altitude_m);
    o << ',';
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',';
    writeNum(o, p.power_dbfs);
    o << ',' << static_cast<int>(p.processing_mode) << ',' << static_cast<int>(p.origin) << ','
      << (p.valid ? 1 : 0) << ',' << (p.edited ? 1 : 0) << '\n';
  }
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::savePrivateRadio(const QString& path, const RadioDataset& d,
                                const QString& localFrameId, QString* error) {
  if (localFrameId.trimmed().isEmpty()) {
    if (error)
      *error = QStringLiteral("A non-empty local frame ID is required.");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(17);
  configureNumericOutput(&o);
  writePrivateHeader(o, QStringLiteral("A"), localFrameId);
  for (const auto& p : d.points) {
    if (p.role != RadioRole::Tx)
      continue;
    o << "#TX," << p.id << ',' << p.timestamp_ns << ",,,,";
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',';
    writeNum(o, p.power_dbfs);
    o << ',' << (p.valid ? 1 : 0) << '\n';
  }
  o << "id,rf_sequence,measurement_segment_id,rf_timestamp_ns,latitude_deg,longitude_deg,"
       "altitude_m,east_m,north_m,up_m,power_dbfs,processing_mode,origin,valid,edited,"
       "generation_kind,interpolation_batch_id,interpolation_method\n";
  for (const auto& p : d.points) {
    if (p.role == RadioRole::Tx)
      continue;
    o << p.id << ',' << p.rf_sequence << ',' << p.segment_id << ',' << p.timestamp_ns << ",,,,";
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',';
    writeNum(o, p.power_dbfs);
    o << ',' << static_cast<int>(p.processing_mode) << ',' << static_cast<int>(p.origin) << ','
      << (p.valid ? 1 : 0) << ',' << (p.edited ? 1 : 0) << ','
      << static_cast<int>(p.generation_kind) << ',' << p.interpolation_batch_id << ','
      << p.interpolation_method << '\n';
  }
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::loadBuilding(const QString& path, BuildingDataset& out, LoadReport& report) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    report.errors << f.errorString();
    return false;
  }
  const PrivateHeader privateHeader = inspectPrivateHeader(f, report);
  if (privateHeader.is_private && privateHeader.type != QStringLiteral("B") &&
      privateHeader.type != QStringLiteral("RASTER_FRAME")) {
    report.errors << QStringLiteral("The selected private CSV is not Type-B/Raster Frame.");
  }
  if (!report.ok()) {
    f.close();
    return false;
  }
  BuildingDataset d;
  d.private_enu = privateHeader.is_private;
  d.local_frame_id = privateHeader.local_frame_id;
  enum { None, Points, Edges } s = None;
  QTextStream in(&f);
  while (!in.atEnd()) {
    auto r = parseCsvLine(in.readLine());
    if (r.isEmpty() || r[0].trimmed().isEmpty())
      continue;
    if (r[0] == "#ENU_REF" && r.size() >= 4) {
      if (d.private_enu) {
        report.errors << QStringLiteral("Private Type-B/Raster Frame must not contain #ENU_REF.");
        continue;
      }
      bool latitudeOk = false;
      bool longitudeOk = false;
      bool altitudeOk = false;
      const double latitude = number(r[1], &latitudeOk);
      const double longitude = number(r[2], &longitudeOk);
      const double altitude = number(r[3], &altitudeOk);
      if (latitudeOk && longitudeOk && altitudeOk && finiteGeo(latitude, longitude)) {
        d.source_reference = {latitude, longitude, altitude, true, ReferenceSource::File};
      } else {
        report.errors << QStringLiteral("Invalid #ENU_REF in standard Type-B CSV.");
      }
      continue;
    }
    if (r[0] == "#POINTS") {
      s = Points;
      continue;
    }
    if (r[0] == "#EDGES") {
      s = Edges;
      continue;
    }
    if (r[0] == "id" || r[0] == "point_id_a" || r[0].startsWith('#'))
      continue;
    if (s == Points && r.size() >= 11) {
      SurveyPoint p;
      p.id = u64(r[0]);
      p.timestamp_ns = i64(r[1]);
      p.latitude_deg = number(r[2]);
      p.longitude_deg = number(r[3]);
      p.altitude_m = number(r[4]);
      p.east_m = number(r[5]);
      p.north_m = number(r[6]);
      p.up_m = number(r[7]);
      p.fix_quality = r[8].toInt();
      bool originOk = false;
      const int originValue = r[9].toInt(&originOk);
      p.origin = originOk && originValue >= 0 && originValue <= 2
                     ? static_cast<DataOrigin>(originValue)
                     : DataOrigin::Imported;
      p.edited = r[10].toInt() != 0;
      d.points.push_back(p);
    } else if (s == Edges && r.size() >= 2)
      d.edges.push_back({u64(r[0]), u64(r[1])});
    else
      ++report.skipped_rows;
  }
  f.close();
  if (d.points.empty())
    report.errors << QStringLiteral("Type-B contains no building points.");
  if (!d.private_enu && !d.source_reference.valid)
    report.errors << QStringLiteral(
        "Standard Type-B requires a valid #ENU_REF. Use a private Type-B for anonymized data.");
  for (auto& point : d.points) {
    if (d.private_enu) {
      if (!finiteEnu(point.east_m, point.north_m, point.up_m)) {
        report.errors << QStringLiteral("Private Type-B row %1 has incomplete ENU coordinates.")
                             .arg(qulonglong(point.id));
      }
      point.latitude_deg = std::numeric_limits<double>::quiet_NaN();
      point.longitude_deg = std::numeric_limits<double>::quiet_NaN();
      point.altitude_m = std::numeric_limits<double>::quiet_NaN();
      point.geo_status = GeoStatus::PrivateEnu;
      continue;
    }
    if (!finiteGeo(point.latitude_deg, point.longitude_deg) || !std::isfinite(point.altitude_m) ||
        !finiteEnu(point.east_m, point.north_m, point.up_m)) {
      report.errors << QStringLiteral(
                           "Standard Type-B row %1 requires complete WGS84 and ENU coordinates.")
                           .arg(qulonglong(point.id));
      continue;
    }
    if (!checkCoordinateConsistency(point.latitude_deg, point.longitude_deg, point.altitude_m,
                                    point.east_m, point.north_m, point.up_m, d.source_reference)) {
      report.errors << QStringLiteral(
                           "Type-B row %1 has inconsistent WGS84/ENU coordinates (>0.25 m "
                           "horizontal or >0.50 m vertical).")
                           .arg(qulonglong(point.id));
      continue;
    }
    point.geo_status = GeoStatus::MeasuredWgs84;
  }
  if (!report.ok())
    return false;
  out = std::move(d);
  return true;
}

bool CsvCodec::saveBuilding(const QString& path, const BuildingDataset& d, QString* error) {
  if (d.private_enu) {
    if (error)
      *error = QStringLiteral(
          "Private ENU data cannot be exported as standard Type-B. Use Create Private Copy.");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(15);
  configureNumericOutput(&o);
  if (d.source_reference.valid)
    o << "#ENU_REF," << d.source_reference.latitude_deg << ',' << d.source_reference.longitude_deg
      << ',' << d.source_reference.altitude_m << '\n';
  o << "#POINTS\nid,timestamp_ns,latitude_deg,longitude_deg,altitude_m,east_m,north_m,up_m,fix_"
       "quality,origin,edited\n";
  for (const auto& p : d.points) {
    o << p.id << ',' << p.timestamp_ns << ',';
    writeNum(o, p.latitude_deg);
    o << ',';
    writeNum(o, p.longitude_deg);
    o << ',';
    writeNum(o, p.altitude_m);
    o << ',';
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',' << p.fix_quality << ',' << static_cast<int>(p.origin) << ',' << (p.edited ? 1 : 0)
      << '\n';
  }
  o << "#EDGES\npoint_id_a,point_id_b\n";
  for (const auto& e : d.edges)
    o << e.a << ',' << e.b << '\n';
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::savePrivateBuilding(const QString& path, const BuildingDataset& d,
                                   const QString& localFrameId, QString* error) {
  if (localFrameId.trimmed().isEmpty()) {
    if (error)
      *error = QStringLiteral("A non-empty local frame ID is required.");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(17);
  configureNumericOutput(&o);
  writePrivateHeader(o, QStringLiteral("B"), localFrameId);
  o << "#POINTS\n"
       "id,timestamp_ns,latitude_deg,longitude_deg,altitude_m,east_m,north_m,up_m,fix_"
       "quality,origin,edited\n";
  for (const auto& p : d.points) {
    o << p.id << ',' << p.timestamp_ns << ",,,,";
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',' << p.fix_quality << ',' << static_cast<int>(p.origin) << ',' << (p.edited ? 1 : 0)
      << '\n';
  }
  o << "#EDGES\npoint_id_a,point_id_b\n";
  for (const auto& edge : d.edges)
    o << edge.a << ',' << edge.b << '\n';
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::loadBuildingSource(const QString& path, BuildingSource& out, LoadReport& report) {
  BuildingSource source;
  if (!loadBuilding(path, source.building, report))
    return false;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    report.errors << f.errorString();
    return false;
  }
  QTextStream in(&f);
  bool frameMarker = false, haveOrigin = false, haveBounds = false, haveOptions = false;
  while (!in.atEnd()) {
    const auto row = parseCsvLine(in.readLine());
    if (row.isEmpty())
      continue;
    if (row[0] == "#B210POST_RASTER_FRAME" || row[0] == "#B210POST_PRIVATE_RASTER_FRAME") {
      frameMarker = true;
      const int version = row.value(1).toInt();
      if (version != 1 && version != 2)
        report.errors << QStringLiteral("Unsupported Raster Frame version.");
    } else if (row[0] == "#PROJECT_ORIGIN" && row.size() >= 5) {
      bool a = false, b = false, c = false;
      const double lat = number(row[1], &a), lon = number(row[2], &b), alt = number(row[3], &c);
      if (a && b && c && finiteGeo(lat, lon)) {
        source.project_reference = {lat, lon, alt, true, ReferenceSource::File};
        source.origin_point_id = u64(row[4]);
        haveOrigin = true;
      }
    } else if (row[0] == "#ORIGIN_POINT_ID" && row.size() >= 2) {
      source.origin_point_id = u64(row[1]);
    } else if (row[0] == "#RASTER_BOUNDS" && row.size() >= 5) {
      bool a = false, b = false, c = false, d = false;
      source.region.left_m = number(row[1], &a);
      source.region.right_m = number(row[2], &b);
      source.region.bottom_m = number(row[3], &c);
      source.region.top_m = number(row[4], &d);
      haveBounds = a && b && c && d && source.region.left_m >= 0 && source.region.right_m > 0 &&
                   source.region.bottom_m >= 0 && source.region.top_m > 0;
    } else if (row[0] == "#RASTER_OPTIONS" && row.size() >= 5) {
      bool a = false, b = false, c = false, d = false;
      source.region.meters_per_pixel = number(row[1], &a);
      source.power_min_dbfs = number(row[2], &b);
      source.power_max_dbfs = number(row[3], &c);
      source.default_power_dbfs = number(row[4], &d);
      haveOptions = a && b && c && d && source.region.meters_per_pixel > 0 &&
                    source.power_max_dbfs > source.power_min_dbfs;
    } else if (row[0] == "#RASTER_ROTATION_DEG" && row.size() >= 2) {
      bool ok = false;
      const double rotation = number(row[1], &ok);
      if (!ok || !std::isfinite(rotation)) {
        report.errors << QStringLiteral("Raster Frame rotation is not finite.");
      } else {
        source.region.rotation_deg = rotation;
      }
    }
  }
  f.close();
  if (frameMarker) {
    source.kind = source.building.private_enu ? BuildingFileKind::PrivateRasterFrame
                                              : BuildingFileKind::RasterFrame;
    source.has_raster_frame =
        (source.building.private_enu || haveOrigin) && haveBounds && haveOptions;
    if (!source.has_raster_frame)
      report.errors << QStringLiteral(
          "Raster Frame CSV is missing a valid origin, bounds, or raster options record.");
  } else if (source.building.private_enu) {
    source.kind = BuildingFileKind::PrivateTypeB;
  }
  if (!report.ok())
    return false;
  out = std::move(source);
  return true;
}

bool CsvCodec::saveRasterFrame(const QString& path, const BuildingDataset& d,
                               const EnuReference& ref, std::uint64_t originPointId,
                               const RasterRegion& region, double powerMin, double powerMax,
                               double defaultPower, QString* error) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(15);
  configureNumericOutput(&o);
  o << "#B210POST_RASTER_FRAME,2\n";
  o << "#PROJECT_ORIGIN," << ref.latitude_deg << ',' << ref.longitude_deg << ',' << ref.altitude_m
    << ',' << originPointId << '\n';
  o << "#RASTER_BOUNDS," << region.left_m << ',' << region.right_m << ',' << region.bottom_m << ','
    << region.top_m << '\n';
  o << "#RASTER_OPTIONS," << region.meters_per_pixel << ',' << powerMin << ',' << powerMax << ','
    << defaultPower << '\n';
  o << "#RASTER_ROTATION_DEG," << region.rotation_deg << '\n';
  o << "#ENU_REF," << ref.latitude_deg << ',' << ref.longitude_deg << ',' << ref.altitude_m << '\n';
  o << "#POINTS\nid,timestamp_ns,latitude_deg,longitude_deg,altitude_m,east_m,north_m,up_m,fix_"
       "quality,origin,edited\n";
  for (const auto& p : d.points) {
    o << p.id << ',' << p.timestamp_ns << ',';
    writeNum(o, p.latitude_deg);
    o << ',';
    writeNum(o, p.longitude_deg);
    o << ',';
    writeNum(o, p.altitude_m);
    o << ',';
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',' << p.fix_quality << ',' << static_cast<int>(p.origin) << ',' << (p.edited ? 1 : 0)
      << '\n';
  }
  o << "#EDGES\npoint_id_a,point_id_b\n";
  for (const auto& e : d.edges)
    o << e.a << ',' << e.b << '\n';
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::savePrivateRasterFrame(const QString& path, const BuildingDataset& d,
                                      const QString& localFrameId, std::uint64_t originPointId,
                                      const RasterRegion& region, double powerMin, double powerMax,
                                      double defaultPower, QString* error) {
  if (localFrameId.trimmed().isEmpty()) {
    if (error)
      *error = QStringLiteral("A non-empty local frame ID is required.");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = f.errorString();
    return false;
  }
  QTextStream o(&f);
  o.setCodec("UTF-8");
  // o.setRealNumberPrecision(17);
  configureNumericOutput(&o);
  writePrivateHeader(o, QStringLiteral("RASTER_FRAME"), localFrameId);
  o << "#ORIGIN_POINT_ID," << originPointId << '\n';
  o << "#RASTER_BOUNDS," << region.left_m << ',' << region.right_m << ',' << region.bottom_m << ','
    << region.top_m << '\n';
  o << "#RASTER_OPTIONS," << region.meters_per_pixel << ',' << powerMin << ',' << powerMax << ','
    << defaultPower << '\n';
  o << "#RASTER_ROTATION_DEG," << region.rotation_deg << '\n';
  o << "#POINTS\n"
       "id,timestamp_ns,latitude_deg,longitude_deg,altitude_m,east_m,north_m,up_m,fix_"
       "quality,origin,edited\n";
  for (const auto& p : d.points) {
    o << p.id << ',' << p.timestamp_ns << ",,,,";
    writeNum(o, p.east_m);
    o << ',';
    writeNum(o, p.north_m);
    o << ',';
    writeNum(o, p.up_m);
    o << ',' << p.fix_quality << ',' << static_cast<int>(p.origin) << ',' << (p.edited ? 1 : 0)
      << '\n';
  }
  o << "#EDGES\npoint_id_a,point_id_b\n";
  for (const auto& edge : d.edges)
    o << edge.a << ',' << edge.b << '\n';
  if (!f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}

bool CsvCodec::createSyntheticWgs84Copies(const RadioDataset& privateRadio,
                                          const BuildingDataset& privateBuilding,
                                          const EnuReference& reference, RadioDataset& radioCopy,
                                          BuildingDataset& buildingCopy, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error)
      *error = message;
    return false;
  };
  if (!privateRadio.private_enu || !privateBuilding.private_enu) {
    return fail(QStringLiteral("Both Type-A and Type-B must use Private ENU coordinates."));
  }
  if (privateRadio.points.empty() || privateBuilding.points.empty()) {
    return fail(QStringLiteral("Both Private Type-A and Private Type-B must contain points."));
  }
  if (privateRadio.local_frame_id.trimmed().isEmpty() ||
      privateRadio.local_frame_id != privateBuilding.local_frame_id) {
    return fail(
        QStringLiteral("Private Type-A and Type-B must have the same non-empty LOCAL_FRAME_ID."));
  }
  if (!reference.valid || !finiteGeo(reference.latitude_deg, reference.longitude_deg) ||
      !std::isfinite(reference.altitude_m)) {
    return fail(QStringLiteral("The synthetic WGS84 anchor is invalid."));
  }

  RadioDataset convertedRadio = privateRadio;
  BuildingDataset convertedBuilding = privateBuilding;
  EnuReference syntheticReference = reference;
  syntheticReference.source = ReferenceSource::SettingsFallback;

  convertedRadio.reference = syntheticReference;
  convertedRadio.reference_was_synthetic = true;
  convertedRadio.private_enu = false;
  convertedRadio.local_frame_id.clear();
  for (auto& point : convertedRadio.points) {
    if (!finiteEnu(point.east_m, point.north_m, point.up_m)) {
      return fail(QStringLiteral("Private Type-A point %1 has incomplete ENU coordinates.")
                      .arg(qulonglong(point.id)));
    }
    const auto wgs = CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m,
                                                     syntheticReference);
    if (!finiteGeo(wgs.latitude_deg, wgs.longitude_deg) || !std::isfinite(wgs.altitude_m)) {
      return fail(QStringLiteral("Synthetic WGS84 conversion failed for Type-A point %1.")
                      .arg(qulonglong(point.id)));
    }
    point.latitude_deg = wgs.latitude_deg;
    point.longitude_deg = wgs.longitude_deg;
    point.altitude_m = wgs.altitude_m;
    point.geo_status = GeoStatus::SyntheticWgs84;
    if (!checkCoordinateConsistency(point.latitude_deg, point.longitude_deg, point.altitude_m,
                                    point.east_m, point.north_m, point.up_m, syntheticReference)) {
      return fail(QStringLiteral("Synthetic WGS84 consistency check failed for Type-A point %1.")
                      .arg(qulonglong(point.id)));
    }
  }

  convertedBuilding.source_reference = syntheticReference;
  convertedBuilding.private_enu = false;
  convertedBuilding.local_frame_id.clear();
  for (auto& point : convertedBuilding.points) {
    if (!finiteEnu(point.east_m, point.north_m, point.up_m)) {
      return fail(QStringLiteral("Private Type-B point %1 has incomplete ENU coordinates.")
                      .arg(qulonglong(point.id)));
    }
    const auto wgs = CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m,
                                                     syntheticReference);
    if (!finiteGeo(wgs.latitude_deg, wgs.longitude_deg) || !std::isfinite(wgs.altitude_m)) {
      return fail(QStringLiteral("Synthetic WGS84 conversion failed for Type-B point %1.")
                      .arg(qulonglong(point.id)));
    }
    point.latitude_deg = wgs.latitude_deg;
    point.longitude_deg = wgs.longitude_deg;
    point.altitude_m = wgs.altitude_m;
    point.geo_status = GeoStatus::SyntheticWgs84;
    if (!checkCoordinateConsistency(point.latitude_deg, point.longitude_deg, point.altitude_m,
                                    point.east_m, point.north_m, point.up_m, syntheticReference)) {
      return fail(QStringLiteral("Synthetic WGS84 consistency check failed for Type-B point %1.")
                      .arg(qulonglong(point.id)));
    }
  }

  radioCopy = std::move(convertedRadio);
  buildingCopy = std::move(convertedBuilding);
  if (error)
    error->clear();
  return true;
}

void CsvCodec::alignRadio(RadioDataset& d, const EnuReference& target) {
  if (d.private_enu) {
    d.reference = target;
    return;
  }
  const bool synthetic = d.reference_was_synthetic;
  for (auto& p : d.points) {
    if (p.geo_status == GeoStatus::SyntheticWgs84 && finiteEnu(p.east_m, p.north_m)) {
      // Anonymous points keep ENU authoritative when a Type-B/Frame origin
      // replaces the temporary Settings origin.
      const auto w = CoordinateTransform::enuToWgs84(p.east_m, p.north_m,
                                                     std::isfinite(p.up_m) ? p.up_m : 0, target);
      p.latitude_deg = w.latitude_deg;
      p.longitude_deg = w.longitude_deg;
      p.altitude_m = w.altitude_m;
      continue;
    }
    if (!finiteGeo(p.latitude_deg, p.longitude_deg) && finiteEnu(p.east_m, p.north_m) &&
        d.reference.valid) {
      const auto w = CoordinateTransform::enuToWgs84(
          p.east_m, p.north_m, std::isfinite(p.up_m) ? p.up_m : 0, d.reference);
      p.latitude_deg = w.latitude_deg;
      p.longitude_deg = w.longitude_deg;
      p.altitude_m = w.altitude_m;
    }
    if (finiteGeo(p.latitude_deg, p.longitude_deg)) {
      const auto e =
          CoordinateTransform::wgs84ToEnu(p.latitude_deg, p.longitude_deg, p.altitude_m, target);
      p.east_m = e.east_m;
      p.north_m = e.north_m;
      p.up_m = e.up_m;
      p.geo_status = synthetic ? GeoStatus::SyntheticWgs84 : GeoStatus::DerivedFromFileReference;
    }
  }
  d.reference = target;
}

bool CsvCodec::rebaseRadioToFirstRx(RadioDataset& data, EnuReference& reference,
                                    std::uint64_t& originPointId) {
  auto first = std::find_if(data.points.begin(), data.points.end(), [](const RadioPoint& point) {
    return point.role == RadioRole::Rx && point.valid && finiteEnu(point.east_m, point.north_m);
  });
  if (first == data.points.end()) {
    first = std::find_if(data.points.begin(), data.points.end(), [](const RadioPoint& point) {
      return finiteEnu(point.east_m, point.north_m);
    });
  }
  if (first == data.points.end())
    return false;

  originPointId = first->id;
  if (data.private_enu) {
    reference = data.reference;
    return true;
  }
  if (!finiteGeo(first->latitude_deg, first->longitude_deg) || !std::isfinite(first->altitude_m)) {
    return false;
  }

  reference = {first->latitude_deg, first->longitude_deg, first->altitude_m, true,
               ReferenceSource::File};
  alignRadio(data, reference);
  first->east_m = 0.0;
  first->north_m = 0.0;
  first->up_m = 0.0;
  return true;
}

void CsvCodec::alignBuilding(BuildingDataset& d, const EnuReference& target, LoadReport* report) {
  if (d.private_enu) {
    d.source_reference = target;
    return;
  }
  for (auto& p : d.points) {
    bool geo = finiteGeo(p.latitude_deg, p.longitude_deg);
    if (!geo && finiteEnu(p.east_m, p.north_m) && d.source_reference.valid) {
      auto w = CoordinateTransform::enuToWgs84(
          p.east_m, p.north_m, std::isfinite(p.up_m) ? p.up_m : 0, d.source_reference);
      p.latitude_deg = w.latitude_deg;
      p.longitude_deg = w.longitude_deg;
      p.altitude_m = w.altitude_m;
      geo = true;
    }
    if (geo) {
      auto e =
          CoordinateTransform::wgs84ToEnu(p.latitude_deg, p.longitude_deg, p.altitude_m, target);
      p.east_m = e.east_m;
      p.north_m = e.north_m;
      p.up_m = e.up_m;
      p.geo_status = target.source == ReferenceSource::File ? GeoStatus::MeasuredWgs84
                                                            : GeoStatus::SyntheticWgs84;
    } else if (finiteEnu(p.east_m, p.north_m)) {
      auto w = CoordinateTransform::enuToWgs84(p.east_m, p.north_m,
                                               std::isfinite(p.up_m) ? p.up_m : 0, target);
      p.latitude_deg = w.latitude_deg;
      p.longitude_deg = w.longitude_deg;
      p.altitude_m = w.altitude_m;
      p.geo_status = GeoStatus::SyntheticWgs84;
      if (report)
        report->warnings << QStringLiteral(
            "A Type-B point without WGS84/source reference was interpreted directly in the project "
            "ENU frame.");
    } else
      p.geo_status = GeoStatus::Unavailable;
  }
  d.source_reference = target;
}

RecoveryResult CsvCodec::recoverRawTimeline(const QString& path, const EnuReference& fallback) {
  RecoveryResult z;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    z.report.errors << f.errorString();
    return z;
  }
  QTextStream in(&f);
  if (in.atEnd()) {
    z.report.errors << "Empty raw timeline.";
    f.close();
    return z;
  }
  auto h = header(parseCsvLine(in.readLine()));
  QStringList required = {"record_type",        "measurement_segment_id",
                          "host_timestamp_ns",  "rf_sequence",
                          "power_dbfs",         "processing_mode",
                          "rf_valid",           "latitude_deg",
                          "longitude_deg",      "altitude_m",
                          "gnss_valid",         "event_name",
                          "related_rf_sequence"};
  for (const auto& n : required)
    if (!h.contains(n))
      z.report.errors << "Missing column: " + n;
  if (!z.report.ok()) {
    f.close();
    return z;
  }
  std::vector<RawGnss> gs;
  QHash<qulonglong, RawRf> rf;
  QHash<qulonglong, bool> accepted, discarded;
  while (!in.atEnd()) {
    bool ok = false;
    auto r = parseCsvLine(in.readLine(), &ok);
    if (!ok) {
      z.report.warnings << "Truncated final CSV row ignored.";
      continue;
    }
    QString type = field(r, h, "record_type");
    if (type == "GNSS_FIX") {
      RawGnss g;
      g.t = i64(field(r, h, "host_timestamp_ns"));
      g.segment = u64(field(r, h, "measurement_segment_id"));
      g.la = number(field(r, h, "latitude_deg"));
      g.lo = number(field(r, h, "longitude_deg"));
      g.al = number(field(r, h, "altitude_m"));
      g.valid = field(r, h, "gnss_valid").toInt() != 0;
      gs.push_back(g);
    } else if (type == "RF_POWER") {
      RawRf x;
      x.seq = u64(field(r, h, "rf_sequence"));
      x.segment = u64(field(r, h, "measurement_segment_id"));
      x.t = i64(field(r, h, "host_timestamp_ns"));
      x.power = number(field(r, h, "power_dbfs"));
      x.mode = modeFrom(field(r, h, "processing_mode"));
      x.valid = field(r, h, "rf_valid").toInt() != 0;
      x.status = field(r, h, "status");
      rf.insert(x.seq, x);
      ++z.raw_rf_rows;
    } else if (type == "EVENT") {
      auto q = u64(field(r, h, "related_rf_sequence"));
      auto e = field(r, h, "event_name");
      if (e == "RF_ACCEPTED")
        accepted[q] = true;
      else if (e == "RF_DISCARDED")
        discarded[q] = true;
    }
  }
  f.close();
  std::sort(gs.begin(), gs.end(), [](auto& a, auto& b) {
    return a.t < b.t;
  });
  z.dataset.reference = fallback;
  z.dataset.reference.valid = true;
  z.dataset.reference.source = ReferenceSource::SettingsFallback;
  z.dataset.reference_was_synthetic = true;
  QList<qulonglong> keys = rf.keys();
  std::sort(keys.begin(), keys.end());
  qulonglong id = 1;
  for (auto seq : keys) {
    const auto x = rf.value(seq);
    if (!accepted.value(seq) || discarded.value(seq) || !x.valid || !std::isfinite(x.power)) {
      ++z.rejected_rows;
      continue;
    }
    auto it = std::lower_bound(gs.begin(), gs.end(), x.t, [](const RawGnss& g, qint64 t) {
      return g.t < t;
    });
    const RawGnss *a = nullptr, *b = nullptr;
    if (it != gs.end() && it->t == x.t) {
      b = &*it;
      if (it != gs.begin())
        a = &*(it - 1);
      if (it + 1 != gs.end() && (it + 1)->valid) {
        a = &*it;
        b = &*(it + 1);
      }
    } else if (it != gs.end() && it != gs.begin()) {
      b = &*it;
      a = &*(it - 1);
    }
    if (!a || !b || !a->valid || !b->valid || b->t <= a->t || a->segment != x.segment ||
        b->segment != x.segment || !finiteGeo(a->la, a->lo) || !finiteGeo(b->la, b->lo)) {
      z.report.warnings
          << QString("RF %1 was ACCEPTED but its GNSS bracket cannot be reconstructed.").arg(seq);
      continue;
    }
    double q = double(x.t - a->t) / double(b->t - a->t);
    RadioPoint p;
    p.id = id++;
    p.rf_sequence = x.seq;
    p.segment_id = x.segment;
    p.timestamp_ns = x.t;
    p.latitude_deg = a->la + q * (b->la - a->la);
    p.longitude_deg = a->lo + q * (b->lo - a->lo);
    p.altitude_m = std::isfinite(a->al) && std::isfinite(b->al) ? a->al + q * (b->al - a->al)
                                                                : fallback.altitude_m;
    p.power_dbfs = x.power;
    p.processing_mode = x.mode;
    p.origin = DataOrigin::Measured;
    p.valid = true;
    p.geo_status = GeoStatus::MeasuredWgs84;
    z.dataset.points.push_back(p);
    ++z.confirmed_points;
  }
  if (!z.dataset.points.empty()) {
    const auto& p = z.dataset.points.front();
    z.dataset.reference = {p.latitude_deg, p.longitude_deg, p.altitude_m, true,
                           ReferenceSource::File};
    z.dataset.reference_was_synthetic = false;
    for (auto& q : z.dataset.points) {
      auto e = CoordinateTransform::wgs84ToEnu(q.latitude_deg, q.longitude_deg, q.altitude_m,
                                               z.dataset.reference);
      q.east_m = e.east_m;
      q.north_m = e.north_m;
      q.up_m = e.up_m;
    }
  }
  return z;
}

}  // namespace b210post
