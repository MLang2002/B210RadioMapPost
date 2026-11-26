#include "Models.hpp"

#include <QtGlobal>
#include <QBrush>
#include <QColor>
#include <QVector>
#include <cmath>
#include <limits>

#include "CoordinateTransform.hpp"

#ifndef MY_QT_DEPRECATED
#if __cplusplus >= 201402L
#define MY_QT_DEPRECATED(msg) [[deprecated(msg)]]
#else
#if defined(Q_DECL_DEPRECATED_X)
#define MY_QT_DEPRECATED(msg) Q_DECL_DEPRECATED_X(msg)
#elif defined(Q_DECL_DEPRECATED)
#define MY_QT_DEPRECATED(msg) Q_DECL_DEPRECATED
#elif defined(__GNUC__) || defined(__clang__)
#define MY_QT_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#define MY_QT_DEPRECATED(msg) __declspec(deprecated(msg))
#else
#define MY_QT_DEPRECATED(msg)
#endif
#endif
#endif // MY_QT_DEPRECATED Macro

namespace b210post {
namespace {

enum RadioColumn {
    kRadioId = 0,
    kRadioVisible,
    kRadioRole,
    kRadioRfSequence,
    kRadioSegment,
    kRadioTime,
    kRadioLatitude,
    kRadioLongitude,
    kRadioAltitude,
    kRadioEast,
    kRadioNorth,
    kRadioUp,
    kRadioPower,
    kRadioProcessing,
    kRadioOrigin,
    kRadioValid,
    kRadioEdited,
    kRadioGeneration,
    kRadioInterpolationBatch,
    kRadioInterpolationMethod,
    kRadioGeoStatus,
    kRadioColumnCount
};

enum BuildingColumn {
    kBuildingId = 0,
    kBuildingVisible,
    kBuildingTime,
    kBuildingLatitude,
    kBuildingLongitude,
    kBuildingAltitude,
    kBuildingEast,
    kBuildingNorth,
    kBuildingUp,
    kBuildingFixQuality,
    kBuildingOrigin,
    kBuildingEdited,
    kBuildingGeoStatus,
    kBuildingColumnCount
};

const char* kRadioHeaders[] = {"ID",           "Visible",    "Role",           "RF Seq",
                               "Segment",      "Time [ns]",  "Latitude [deg]", "Longitude [deg]",
                               "Altitude [m]", "East [m]",   "North [m]",      "Up [m]",
                               "Power [dB]",   "Processing", "Origin",         "Valid",
                               "Edited",       "Generation", "Fill Batch",     "Fill Method",
                               "Geo Status"
                              };

const char* kBuildingHeaders[] = {
    "ID",           "Visible",  "Time [ns]", "Latitude [deg]", "Longitude [deg]",
    "Altitude [m]", "East [m]", "North [m]", "Up [m]",         "Fix Quality",
    "Origin",       "Edited",   "Geo Status"
};

QString originName(DataOrigin origin) {
    if (origin == DataOrigin::Manual) {
        return QStringLiteral("Manual");
    }
    if (origin == DataOrigin::Measured) {
        return QStringLiteral("Measured");
    }
    return QStringLiteral("Imported");
}

QString generationName(PointGenerationKind generation) {
    switch (generation) {
    case PointGenerationKind::Original:
        return QStringLiteral("Original");
    case PointGenerationKind::Manual:
        return QStringLiteral("Manual");
    case PointGenerationKind::Interpolated:
        return QStringLiteral("Interpolated");
    }
    return {};
}

[[maybe_unused]]
[[deprecated("Please use preciseNumber function instead!")]]
QVariant finiteNumber(double value) {
    return std::isfinite(value) ? QVariant(value) : QVariant(QStringLiteral("n.a."));
}

QVariant preciseNumber(double value, int role, int displayDecimals) {
    if (!std::isfinite(value))
        return QVariant(QStringLiteral("n.a."));
    if (role == Qt::EditRole) {
        return QVariant(QString::number(value, 'g', std::numeric_limits<double>::max_digits10));
    }

    return QVariant(QString::number(value, 'f', displayDecimals));
}

}  // namespace

RadioTableModel::RadioTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void RadioTableModel::setDataset(RadioDataset* dataset) {
    beginResetModel();
    d_ = dataset;
    endResetModel();
}

int RadioTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !d_ ? 0 : static_cast<int>(d_->points.size());
}

int RadioTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kRadioColumnCount;
}

QVariant RadioTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
            section >= kRadioColumnCount) {
        return {};
    }
    return QString::fromLatin1(kRadioHeaders[section]);
}

QVariant RadioTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !d_ || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& point = d_->points[static_cast<std::size_t>(index.row())];
    if (index.column() == kRadioVisible && role == Qt::CheckStateRole) {
        return point.visible ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ForegroundRole && point.geo_status == GeoStatus::SyntheticWgs84) {
        return QBrush(QColor(190, 110, 0));
    }
    if (role == Qt::ForegroundRole && point.geo_status == GeoStatus::PrivateEnu) {
        return QBrush(QColor(120, 80, 160));
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == kRadioVisible) {
            return QStringLiteral(
                       "Show or hide this marker on the interactive ENU chart. Visibility does not change "
                       "raster calculations or saved Type-A data.");
        }
        if (index.column() == kRadioRole) {
            return QStringLiteral(
                       "Point role. TX and RX share editing operations; TX is excluded from RX power "
                       "averaging.");
        }
        if (index.column() == kRadioGeneration || index.column() == kRadioInterpolationBatch ||
                index.column() == kRadioInterpolationMethod) {
            return QStringLiteral(
                       "In-memory provenance for local fill. Private Type-A v2 preserves it; standard "
                       "B210RadioMap Type-A saves generated points as Manual and cannot preserve the batch "
                       "or method.");
        }
        if (point.geo_status == GeoStatus::SyntheticWgs84) {
            return QStringLiteral(
                       "WGS84 was generated from ENU using the anonymous Settings origin; it is not measured "
                       "GNSS data.");
        }
        if (point.geo_status == GeoStatus::PrivateEnu && index.column() >= kRadioLatitude &&
                index.column() <= kRadioAltitude) {
            return QStringLiteral(
                       "Geographic coordinates were removed. Private ENU is the sole coordinate authority.");
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
    case kRadioId:
        return qulonglong(point.id);
    case kRadioVisible:
        return {};
    case kRadioRole:
        return radioRoleName(point.role);
    case kRadioRfSequence:
        return qulonglong(point.rf_sequence);
    case kRadioSegment:
        return qulonglong(point.segment_id);
    case kRadioTime:
        return qlonglong(point.timestamp_ns);
    case kRadioLatitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden"))
        //                        : finiteNumber(point.latitude_deg);
        return d_->private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.latitude_deg, role, 10);
    case kRadioLongitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden"))
        //                        : finiteNumber(point.longitude_deg);
        return d_ -> private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.longitude_deg, role, 10);
    case kRadioAltitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden")) : finiteNumber(point.altitude_m);
        return d_ -> private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.altitude_m, role, 6);
    case kRadioEast:
        // return finiteNumber(point.east_m);
        return preciseNumber(point.east_m, role, 6);
    case kRadioNorth:
        // return finiteNumber(point.north_m);
        return preciseNumber(point.north_m, role, 6);
    case kRadioUp:
        // return finiteNumber(point.up_m);
        return preciseNumber(point.up_m, role, 6);
    case kRadioPower:
        // return finiteNumber(point.power_dbfs);
        return preciseNumber(point.power_dbfs, role, 6);
    case kRadioProcessing:
        return processingModeName(point.processing_mode);
    case kRadioOrigin:
        return originName(point.origin);
    case kRadioValid:
        return point.valid ? QStringLiteral("yes") : QStringLiteral("no");
    case kRadioEdited:
        return point.edited ? QStringLiteral("yes") : QStringLiteral("no");
    case kRadioGeneration:
        return generationName(point.generation_kind);
    case kRadioInterpolationBatch:
        return point.interpolation_batch_id == 0 ? QVariant(QStringLiteral("n.a."))
               : QVariant(qulonglong(point.interpolation_batch_id));
    case kRadioInterpolationMethod:
        return point.interpolation_method.isEmpty() ? QVariant(QStringLiteral("n.a."))
               : QVariant(point.interpolation_method);
    case kRadioGeoStatus:
        return geoStatusName(point.geo_status);
    default:
        return {};
    }
}

Qt::ItemFlags RadioTableModel::flags(const QModelIndex& index) const {
    auto flags = QAbstractTableModel::flags(index);
    if (!index.isValid()) {
        return flags;
    }
    if (index.column() == kRadioVisible) {
        flags |= Qt::ItemIsUserCheckable;
    }
    const bool privateWgs =
        d_ && d_->private_enu && index.column() >= kRadioLatitude && index.column() <= kRadioAltitude;
    if (!privateWgs && (index.column() == kRadioRole ||
                        (index.column() >= kRadioLatitude && index.column() <= kRadioPower))) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool RadioTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!d_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return false;
    }
    auto& point = d_->points[static_cast<std::size_t>(index.row())];
    if (index.column() == kRadioVisible && role == Qt::CheckStateRole) {
        const bool visible = value.toInt() == Qt::Checked;
        if (point.visible == visible) {
            return true;
        }
        point.visible = visible;
        emit dataChanged(index, index, QVector<int> {Qt::CheckStateRole});
        emit visibilityChanged();
        return true;
    }
    if (role != Qt::EditRole) {
        return false;
    }
    if (index.column() == kRadioRole) {
        const QString text = value.toString().trimmed();
        if (text.compare(QStringLiteral("TX"), Qt::CaseInsensitive) == 0) {
            point.role = RadioRole::Tx;
        } else if (text.compare(QStringLiteral("RX"), Qt::CaseInsensitive) == 0) {
            point.role = RadioRole::Rx;
        } else {
            return false;
        }
    } else {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number)) {
            return false;
        }
        if (index.column() == kRadioLatitude) {
            if (number < -90.0 || number > 90.0) {
                return false;
            }
            point.latitude_deg = number;
        } else if (index.column() == kRadioLongitude) {
            if (number < -180.0 || number > 180.0) {
                return false;
            }
            point.longitude_deg = number;
        } else if (index.column() == kRadioAltitude) {
            point.altitude_m = number;
        } else if (index.column() == kRadioEast) {
            point.east_m = number;
        } else if (index.column() == kRadioNorth) {
            point.north_m = number;
        } else if (index.column() == kRadioUp) {
            point.up_m = number;
        } else if (index.column() == kRadioPower) {
            point.power_dbfs = number;
        } else {
            return false;
        }
        if (index.column() >= kRadioLatitude && index.column() <= kRadioAltitude) {
            const auto enu = CoordinateTransform::wgs84ToEnu(point.latitude_deg, point.longitude_deg,
                             point.altitude_m, d_->reference);
            point.east_m = enu.east_m;
            point.north_m = enu.north_m;
            point.up_m = enu.up_m;
            point.geo_status = d_->reference.source == ReferenceSource::File ? GeoStatus::MeasuredWgs84
                               : GeoStatus::SyntheticWgs84;
        } else if (index.column() <= kRadioUp && d_->private_enu) {
            point.geo_status = GeoStatus::PrivateEnu;
        } else if (index.column() <= kRadioUp) {
            const auto wgs =
                CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m, d_->reference);
            point.latitude_deg = wgs.latitude_deg;
            point.longitude_deg = wgs.longitude_deg;
            point.altitude_m = wgs.altitude_m;
            point.geo_status = d_->reference.source == ReferenceSource::File
                               ? GeoStatus::DerivedFromFileReference
                               : GeoStatus::SyntheticWgs84;
        }
    }
    point.edited = true;
    emit dataChanged(this->index(index.row(), kRadioRole), this->index(index.row(), kRadioGeoStatus));
    emit changed();
    return true;
}

RadioPoint* RadioTableModel::pointAt(int row) {
    return d_ && row >= 0 && row < rowCount() ? &d_->points[static_cast<std::size_t>(row)] : nullptr;
}

int RadioTableModel::rowForId(std::uint64_t id) const {
    if (!d_) {
        return -1;
    }
    for (std::size_t index = 0; index < d_->points.size(); ++index) {
        if (d_->points[index].id == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void RadioTableModel::refresh() {
    beginResetModel();
    endResetModel();
}

BuildingTableModel::BuildingTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void BuildingTableModel::setDataSource(BuildingDataset* dataset,
                                       const EnuReference* projectReference) {
    beginResetModel();
    d_ = dataset;
    ref_ = projectReference;
    endResetModel();
}

int BuildingTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !d_ ? 0 : static_cast<int>(d_->points.size());
}

int BuildingTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kBuildingColumnCount;
}

QVariant BuildingTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
            section >= kBuildingColumnCount) {
        return {};
    }
    return QString::fromLatin1(kBuildingHeaders[section]);
}

QVariant BuildingTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !d_ || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& point = d_->points[static_cast<std::size_t>(index.row())];
    if (index.column() == kBuildingVisible && role == Qt::CheckStateRole) {
        return point.visible ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ForegroundRole && point.geo_status == GeoStatus::SyntheticWgs84) {
        return QBrush(QColor(190, 110, 0));
    }
    if (role == Qt::ForegroundRole && point.geo_status == GeoStatus::PrivateEnu) {
        return QBrush(QColor(120, 80, 160));
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == kBuildingVisible) {
            return QStringLiteral(
                       "Show or hide this vertex marker on the interactive ENU chart. Edges, topology, and "
                       "raster masks remain unchanged.");
        }
        if (point.geo_status == GeoStatus::SyntheticWgs84) {
            return QStringLiteral("Synthetic WGS84 generated from the anonymous project origin.");
        }
        if (point.geo_status == GeoStatus::PrivateEnu) {
            return QStringLiteral(
                       "Geographic coordinates were removed. Private ENU is the sole coordinate authority.");
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
    case kBuildingId:
        return qulonglong(point.id);
    case kBuildingVisible:
        return {};
    case kBuildingTime:
        return qlonglong(point.timestamp_ns);
    case kBuildingLatitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden"))
        //                        : finiteNumber(point.latitude_deg);
        return d_ -> private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.latitude_deg, role, 10);
    case kBuildingLongitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden"))
        //                        : finiteNumber(point.longitude_deg);
        return d_ -> private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.longitude_deg, role, 10);
    case kBuildingAltitude:
        // return d_->private_enu ? QVariant(QStringLiteral("Hidden")) : finiteNumber(point.altitude_m);
        return d_ -> private_enu ? QVariant(QStringLiteral("Hidden")) : preciseNumber(point.altitude_m, role, 6);
    case kBuildingEast:
        // return finiteNumber(point.east_m);
        return preciseNumber(point.east_m, role, 6);
    case kBuildingNorth:
        // return finiteNumber(point.north_m);
        return preciseNumber(point.north_m, role, 6);
    case kBuildingUp:
        // return finiteNumber(point.up_m);
        return preciseNumber(point.up_m, role, 6);
    case kBuildingFixQuality:
        return point.fix_quality;
    case kBuildingOrigin:
        return originName(point.origin);
    case kBuildingEdited:
        return point.edited ? QStringLiteral("yes") : QStringLiteral("no");
    case kBuildingGeoStatus:
        return geoStatusName(point.geo_status);
    default:
        return {};
    }
}

Qt::ItemFlags BuildingTableModel::flags(const QModelIndex& index) const {
    auto flags = QAbstractTableModel::flags(index);
    if (!index.isValid()) {
        return flags;
    }
    if (index.column() == kBuildingVisible) {
        flags |= Qt::ItemIsUserCheckable;
    }
    const bool privateWgs = d_ && d_->private_enu && index.column() >= kBuildingLatitude &&
                            index.column() <= kBuildingAltitude;
    if (!privateWgs && index.column() >= kBuildingLatitude && index.column() <= kBuildingUp) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool BuildingTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!d_ || !ref_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return false;
    }
    auto& point = d_->points[static_cast<std::size_t>(index.row())];
    if (index.column() == kBuildingVisible && role == Qt::CheckStateRole) {
        const bool visible = value.toInt() == Qt::Checked;
        if (point.visible == visible) {
            return true;
        }
        point.visible = visible;
        emit dataChanged(index, index, QVector<int> {Qt::CheckStateRole});
        emit visibilityChanged();
        return true;
    }
    if (role != Qt::EditRole) {
        return false;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return false;
    }
    if (index.column() == kBuildingLatitude) {
        if (number < -90.0 || number > 90.0) {
            return false;
        }
        point.latitude_deg = number;
    } else if (index.column() == kBuildingLongitude) {
        if (number < -180.0 || number > 180.0) {
            return false;
        }
        point.longitude_deg = number;
    } else if (index.column() == kBuildingAltitude) {
        point.altitude_m = number;
    } else if (index.column() == kBuildingEast) {
        point.east_m = number;
    } else if (index.column() == kBuildingNorth) {
        point.north_m = number;
    } else if (index.column() == kBuildingUp) {
        point.up_m = number;
    } else {
        return false;
    }
    if (index.column() >= kBuildingLatitude && index.column() <= kBuildingAltitude) {
        const auto enu = CoordinateTransform::wgs84ToEnu(point.latitude_deg, point.longitude_deg,
                         point.altitude_m, *ref_);
        point.east_m = enu.east_m;
        point.north_m = enu.north_m;
        point.up_m = enu.up_m;
    } else if (d_->private_enu) {
        point.geo_status = GeoStatus::PrivateEnu;
    } else {
        const auto wgs =
            CoordinateTransform::enuToWgs84(point.east_m, point.north_m, point.up_m, *ref_);
        point.latitude_deg = wgs.latitude_deg;
        point.longitude_deg = wgs.longitude_deg;
        point.altitude_m = wgs.altitude_m;
    }
    if (!d_->private_enu) {
        point.geo_status = ref_->source == ReferenceSource::File ? GeoStatus::DerivedFromFileReference
                           : GeoStatus::SyntheticWgs84;
    }
    point.edited = true;
    emit dataChanged(this->index(index.row(), kBuildingLatitude),
                     this->index(index.row(), kBuildingGeoStatus));
    emit changed();
    return true;
}

SurveyPoint* BuildingTableModel::pointAt(int row) {
    return d_ && row >= 0 && row < rowCount() ? &d_->points[static_cast<std::size_t>(row)] : nullptr;
}

int BuildingTableModel::rowForId(std::uint64_t id) const {
    if (!d_) {
        return -1;
    }
    for (std::size_t index = 0; index < d_->points.size(); ++index) {
        if (d_->points[index].id == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void BuildingTableModel::refresh() {
    beginResetModel();
    endResetModel();
}

}  // namespace b210post
