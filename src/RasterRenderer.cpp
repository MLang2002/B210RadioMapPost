#include "RasterRenderer.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace b210post {
namespace {

constexpr double kPi = 3.14159265358979323846;

QPointF worldToLocal(double east, double north, const RasterFrame& frame) {
    const double radians = frame.rotation_deg * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {cosine * east + sine * north, -sine * east + cosine * north};
}

QPointF pixelBoundary(double east, double north, const RasterFrame& f) {
    const auto local = worldToLocal(east, north, f);
    return {(local.x() - f.min_east_m) / f.meters_per_pixel,
            (f.max_north_m - local.y()) / f.meters_per_pixel};
}

QColor scalarColor(int level) {
    const double normalized = std::clamp(level / 255.0, 0.0, 1.0);
    return QColor::fromHsvF((1.0 - normalized) * 0.66, 1.0, 1.0);
}

QImage colorize(const QImage& grayscale, RasterColorMode mode, const QColor& categoryColor) {
    if (mode == RasterColorMode::Grayscale) {
        return grayscale;
    }
    QImage colored(grayscale.size(), QImage::Format_ARGB32);
    for (int y = 0; y < grayscale.height(); ++y) {
        const auto* source = grayscale.constScanLine(y);
        auto* target = reinterpret_cast<QRgb*>(colored.scanLine(y));
        for (int x = 0; x < grayscale.width(); ++x) {
            if (categoryColor.isValid()) {
                const double amount = source[x] / 255.0;
                target[x] =
                    qRgb(qRound(categoryColor.red() * amount), qRound(categoryColor.green() * amount),
                         qRound(categoryColor.blue() * amount));
            } else {
                target[x] = scalarColor(source[x]).rgba();
            }
        }
    }
    return colored;
}

double cubicWeight(double value) {
    constexpr double a = -0.5;
    const double x = std::abs(value);
    if (x <= 1.0) {
        return (a + 2.0) * x * x * x - (a + 3.0) * x * x + 1.0;
    }
    if (x < 2.0) {
        return a * x * x * x - 5.0 * a * x * x + 8.0 * a * x - 4.0 * a;
    }
    return 0.0;
}

QImage bicubicResizeGrayscale(const QImage& source, const QSize& targetSize) {
    QImage target(targetSize, QImage::Format_Grayscale8);
    const double scaleX = static_cast<double>(source.width()) / target.width();
    const double scaleY = static_cast<double>(source.height()) / target.height();
    for (int y = 0; y < target.height(); ++y) {
        const double sourceY = (y + 0.5) * scaleY - 0.5;
        const int baseY = static_cast<int>(std::floor(sourceY));
        auto* output = target.scanLine(y);
        for (int x = 0; x < target.width(); ++x) {
            const double sourceX = (x + 0.5) * scaleX - 0.5;
            const int baseX = static_cast<int>(std::floor(sourceX));
            double weighted = 0.0;
            double totalWeight = 0.0;
            for (int offsetY = -1; offsetY <= 2; ++offsetY) {
                const int sampleY = std::clamp(baseY + offsetY, 0, source.height() - 1);
                const double weightY = cubicWeight(sourceY - (baseY + offsetY));
                const auto* input = source.constScanLine(sampleY);
                for (int offsetX = -1; offsetX <= 2; ++offsetX) {
                    const int sampleX = std::clamp(baseX + offsetX, 0, source.width() - 1);
                    const double weight = weightY * cubicWeight(sourceX - (baseX + offsetX));
                    weighted += weight * input[sampleX];
                    totalWeight += weight;
                }
            }
            const double value = totalWeight == 0.0 ? 0.0 : weighted / totalWeight;
            output[x] = static_cast<uchar>(std::lround(std::clamp(value, 0.0, 255.0)));
        }
    }
    return target;
}

QImage downsample(const QImage& source, const QSize& targetSize, RasterResamplingFilter filter) {
    if (filter == RasterResamplingFilter::Bicubic) {
        return bicubicResizeGrayscale(source, targetSize);
    }
    return source.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

double cross(const QPointF& a, const QPointF& b, const QPointF& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

bool properIntersection(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d) {
    const double ab1 = cross(a, b, c), ab2 = cross(a, b, d), cd1 = cross(c, d, a),
                 cd2 = cross(c, d, b);
    return ((ab1 > 0 && ab2 < 0) || (ab1 < 0 && ab2 > 0)) &&
           ((cd1 > 0 && cd2 < 0) || (cd1 < 0 && cd2 > 0));
}

std::int64_t outwardPixelCount(double distanceM, double metersPerPixel) {
    const double pixels = distanceM / metersPerPixel;
    const double nearest = std::round(pixels);

    // 避免 50 / 0.1 得到 500.00000000000006 而错误扩展一个像素。
    const double tolerance = 1e-9 * std::max(1.0, std::abs(pixels));

    if (std::abs(pixels - nearest) <= tolerance) {
        return static_cast<std::int64_t>(nearest);
    }

    return static_cast<std::int64_t>(std::ceil(pixels));
}

}  // namespace

BuildingValidation RasterRenderer::validateBuilding(const BuildingDataset& b) {
    BuildingValidation result;
    if (b.points.empty()) {
        result.errors << QStringLiteral("No Type-B building points are available.");
        return result;
    }
    std::unordered_map<std::uint64_t, const SurveyPoint*> points;
    for (const auto& p : b.points) {
        if (p.id == 0 || points.count(p.id)) {
            result.errors << QString("Duplicate or zero building point ID: %1.").arg(qulonglong(p.id));
            continue;
        }
        if (!std::isfinite(p.east_m) || !std::isfinite(p.north_m)) {
            result.errors
                    << QString("Building point %1 has no finite ENU coordinate.").arg(qulonglong(p.id));
            continue;
        }
        points[p.id] = &p;
    }
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> adjacency;
    QSet<QString> uniqueEdges;
    for (const auto& e : b.edges) {
        if (e.a == e.b) {
            result.errors << QString("Building edge %1-%2 is a self edge.")
                          .arg(qulonglong(e.a))
                          .arg(qulonglong(e.b));
            continue;
        }
        if (!points.count(e.a) || !points.count(e.b)) {
            result.errors << QString("Building edge %1-%2 references a missing point.")
                          .arg(qulonglong(e.a))
                          .arg(qulonglong(e.b));
            continue;
        }
        const auto lo = std::min(e.a, e.b), hi = std::max(e.a, e.b);
        const QString key = QString::number(lo) + ":" + QString::number(hi);
        if (uniqueEdges.contains(key)) {
            result.errors
                    << QString("Duplicate building edge %1-%2.").arg(qulonglong(e.a)).arg(qulonglong(e.b));
            continue;
        }
        uniqueEdges.insert(key);
        adjacency[e.a].push_back(e.b);
        adjacency[e.b].push_back(e.a);
    }
    if (adjacency.empty()) {
        // result.errors << QStringLiteral("No building edges are available.");
        result.errors << QStringLiteral(
                          "No building edges are available. The imported Type-B contains %1 point(s), but no closed building boundary."
                      ).arg(qulonglong(points.size()));
        return result;
    }
    std::size_t isolatedCount = 0;
    std::size_t endpointCount = 0;
    std::size_t branchedCount = 0;
    QStringList examplePointIds;

    constexpr std::size_t kMaxExamples = 8;

    for (const auto& entry : points) {
        const auto found = adjacency.find(entry.first);
        const auto degree = found == adjacency.end() ? size_t(0) : found->second.size();
        // if (degree != 2)
        //   result.errors << QString(
        //                        "Building point %1 has degree %2; a closed boundary requires degree 2.")
        //                        .arg(qulonglong(entry.first))
        //                        .arg(qulonglong(degree));
        if (degree == 2)
            continue;

        if (degree == 0) {
            ++isolatedCount;
        } else if (degree == 1) {
            ++endpointCount;
        } else {
            ++branchedCount;
        }

        if (examplePointIds.size() < static_cast<int>(kMaxExamples)) {
            examplePointIds.append(QStringLiteral("%1(degree=%2)").arg(qulonglong(entry.first)).arg(qulonglong(degree)));
        }
    }
    const std::size_t invalidDegreeCount = isolatedCount + endpointCount + branchedCount;
    if (invalidDegreeCount > 0) {
        result.errors << QStringLiteral(
                          "The Type-B layout is not closed: %1 point(s) have invalid degree. "
                          "Isonlated=%2, endpoints=%3, branched=%4. "
                          "Enery point in a closed boundary must have degree 2."
                      ).arg(qulonglong(invalidDegreeCount)).arg(qulonglong(isolatedCount)).arg(qulonglong(endpointCount))
                      .arg(qulonglong(branchedCount));
        if (!examplePointIds.isEmpty()) {
            result.errors << QStringLiteral("Example: %1").arg(examplePointIds.join(QStringLiteral(", ")));
        }
        return result;
    }
    // if (!result.errors.isEmpty())
    //   return result;
    std::unordered_set<std::uint64_t> visited;
    for (const auto& entry : adjacency) {
        const auto start = entry.first;
        if (visited.count(start))
            continue;
        std::vector<std::uint64_t> loop;
        std::uint64_t previous = 0, current = start;
        do {
            if (visited.count(current) && current != start) {
                result.errors << QStringLiteral("A building boundary repeats a vertex before closing.");
                return result;
            }
            visited.insert(current);
            loop.push_back(current);
            const auto& nexts = adjacency[current];
            const auto next = nexts[0] == previous ? nexts[1] : nexts[0];
            previous = current;
            current = next;
            if (loop.size() > adjacency.size() + 1) {
                result.errors << QStringLiteral("A building boundary does not close.");
                return result;
            }
        } while (current != start);
        if (loop.size() < 3) {
            result.errors << QStringLiteral("A building polygon requires at least three vertices.");
            return result;
        }
        double area = 0;
        for (size_t i = 0; i < loop.size(); ++i) {
            const auto* a = points[loop[i]];
            const auto* b2 = points[loop[(i + 1) % loop.size()]];
            area += a->east_m * b2->north_m - b2->east_m * a->north_m;
        }
        if (std::abs(area) < 1e-8) {
            result.errors << QStringLiteral("A building polygon has zero area.");
            return result;
        }
        // check cross

        // for (size_t i = 0; i < loop.size(); ++i)
        //   for (size_t j = i + 1; j < loop.size(); ++j) {
        //     if (j == i || j == i + 1 || (i == 0 && j + 1 == loop.size()))
        //       continue;
        //     const auto* a = points[loop[i]];
        //     const auto* b2 = points[loop[(i + 1) % loop.size()]];
        //     const auto* c = points[loop[j]];
        //     const auto* d = points[loop[(j + 1) % loop.size()]];
        //     if (properIntersection({a->east_m, a->north_m}, {b2->east_m, b2->north_m},
        //                            {c->east_m, c->north_m}, {d->east_m, d->north_m})) {
        //       result.errors << QStringLiteral("A building polygon contains crossing edges.");
        //       return result;
        //     }
        //   }

        constexpr std::size_t kMaxAutomaticIntersectionVertices = 5000;
        if (loop.size() <= kMaxAutomaticIntersectionVertices) {
            for (std::size_t i = 0; i < loop.size(); i++) {
                for (std::size_t j = i + 1; j < loop.size(); j++) {
                    if (j == i || j == i + 1 || (i == 0 && j + 1 == loop.size()))
                        continue;
                    const auto* a = points.at(loop[i]);
                    const auto* b = points.at(loop[(i + 1) % loop.size()]);
                    const auto* c = points.at(loop[j]);
                    const auto* d = points.at(loop[(j + 1) % loop.size()]);
                    if (properIntersection({a->east_m, a->north_m}, {b->east_m, b->north_m},
                {c->east_m, c->north_m}, {d->east_m, d->north_m})) {
                        result.errors << QStringLiteral("A building polygon contains crossing edges.");
                        return result;
                    }
                }
            }
        } else {
            result.errors << QStringLiteral("The boundary contains %1 vertices. Exhastive crossing-edge "
                                            "validation was skipped to keep the interface responsive."
                                           ).arg(qulonglong(loop.size()));
        }

        result.loops.push_back(std::move(loop));
    }
    result.valid = !result.loops.empty();
    return result;
}

RasterFrame RasterRenderer::buildFrame(const RasterRegion& region, QString* error) {
    RasterFrame frame;
    if (error) {
        error->clear();
    }

    if (!(region.left_m >= 0.0 && region.right_m > 0.0 && region.bottom_m >= 0.0 &&
            region.top_m > 0.0 && region.meters_per_pixel > 0.0 &&
            std::isfinite(region.rotation_deg))) {
        if (error) {
            *error = QStringLiteral(
                         "Left/bottom offsets must be non-negative; "
                         "right/top offsets and resolution must be positive.");
        }
        return frame;
    }

    const std::int64_t leftPixels = outwardPixelCount(region.left_m, region.meters_per_pixel);
    const std::int64_t rightPixels = outwardPixelCount(region.right_m, region.meters_per_pixel);
    const std::int64_t bottomPixels = outwardPixelCount(region.bottom_m, region.meters_per_pixel);
    const std::int64_t topPixels = outwardPixelCount(region.top_m, region.meters_per_pixel);

    const std::int64_t widthPixels = leftPixels + rightPixels;
    const std::int64_t heightPixels = bottomPixels + topPixels;

    if (widthPixels < 1 || heightPixels < 1) {
        if (error) {
            *error = QStringLiteral("The raster region is smaller than one pixel.");
        }
        return frame;
    }

    if (widthPixels > 100000 || heightPixels > 100000 || widthPixels * heightPixels > 500000000LL) {
        if (error) {
            *error = QStringLiteral("Raster dimensions are too large.");
        }
        return frame;
    }

    // 使用扩展后的实际物理边界。
    frame.min_east_m = -static_cast<double>(leftPixels) * region.meters_per_pixel;
    frame.max_east_m = static_cast<double>(rightPixels) * region.meters_per_pixel;
    frame.min_north_m = -static_cast<double>(bottomPixels) * region.meters_per_pixel;
    frame.max_north_m = static_cast<double>(topPixels) * region.meters_per_pixel;

    frame.meters_per_pixel = region.meters_per_pixel;
    frame.width_px = static_cast<int>(widthPixels);
    frame.height_px = static_cast<int>(heightPixels);
    frame.rotation_deg = std::remainder(region.rotation_deg, 360.0);

    return frame;
}

QPointF RasterRenderer::worldToRasterLocal(double east, double north, const RasterFrame& frame) {
    return worldToLocal(east, north, frame);
}

QPointF RasterRenderer::rasterLocalToWorld(double localX, double localY, const RasterFrame& frame) {
    const double radians = frame.rotation_deg * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {cosine * localX - sine * localY, sine * localX + cosine * localY};
}

RenderBundle RasterRenderer::render(const RadioDataset& radio, const BuildingDataset& building,
                                    const RasterFrame& frame, double defaultPower, double powerMin,
                                    double powerMax, const RasterRenderOptions& requestedOptions) {
    RenderBundle bundle;
    bundle.frame = frame;
    RasterRenderOptions options = requestedOptions;
    if (options.supersampling_factor != 2 && options.supersampling_factor != 4 &&
            options.supersampling_factor != 8) {
        options.supersampling_factor = 4;
        bundle.warnings << QStringLiteral("Unsupported supersampling factor; 4x was used.");
    }
    bundle.default_power_dbfs = defaultPower;
    bundle.power_min_dbfs = powerMin;
    bundle.power_max_dbfs = powerMax;
    const qint64 highWidth = qint64(frame.width_px) * options.supersampling_factor;
    const qint64 highHeight = qint64(frame.height_px) * options.supersampling_factor;
    if (options.smooth_export &&
            (highWidth > 32767 || highHeight > 32767 || highWidth * highHeight > 200000000LL)) {
        options.smooth_export = false;
        bundle.warnings << QStringLiteral(
                            "Supersampling exceeded the safe temporary-image limit; exact-size unsmoothed output "
                            "was generated.");
    }
    bundle.render_options = options;

    QImage buildingMask(frame.width_px, frame.height_px, QImage::Format_Grayscale8);
    buildingMask.fill(0);
    const auto validation = validateBuilding(building);
    std::unordered_map<std::uint64_t, const SurveyPoint*> buildingPoints;
    for (const auto& point : building.points) {
        buildingPoints[point.id] = &point;
    }
    if (validation.valid) {
        QPainter painter(&buildingMask);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        for (const auto& loop : validation.loops) {
            QPolygonF polygon;
            for (const auto id : loop) {
                const auto* point = buildingPoints[id];
                polygon << pixelBoundary(point->east_m, point->north_m, frame);
            }
            painter.drawPolygon(polygon);
        }
    } else if (!building.points.empty()) {
        bundle.warnings.append(validation.errors);
    }

    QImage buildingPresentation = buildingMask;
    if (options.smooth_export && validation.valid) {
        const int factor = options.supersampling_factor;
        QImage high(frame.width_px * factor, frame.height_px * factor, QImage::Format_Grayscale8);
        high.fill(0);
        QPainter painter(&high);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        for (const auto& loop : validation.loops) {
            QPolygonF polygon;
            for (const auto id : loop) {
                const auto* point = buildingPoints[id];
                polygon << pixelBoundary(point->east_m, point->north_m, frame) * factor;
            }
            painter.drawPolygon(polygon);
        }
        painter.end();
        buildingPresentation = downsample(high, buildingMask.size(), options.resampling_filter);
    }

    QImage txMask(frame.width_px, frame.height_px, QImage::Format_Grayscale8);
    txMask.fill(0);
    for (const auto& point : radio.points) {
        if (point.role != RadioRole::Tx || !std::isfinite(point.east_m) ||
                !std::isfinite(point.north_m)) {
            continue;
        }
        const auto pixel = pixelBoundary(point.east_m, point.north_m, frame);
        const int x = static_cast<int>(std::floor(pixel.x()));
        const int y = static_cast<int>(std::floor(pixel.y()));
        if (x >= 0 && y >= 0 && x < frame.width_px && y < frame.height_px) {
            txMask.scanLine(y)[x] = 255;
        }
    }

    QImage txPresentation = txMask;
    if (options.smooth_export) {
        const int factor = options.supersampling_factor;
        QImage high(frame.width_px * factor, frame.height_px * factor, QImage::Format_Grayscale8);
        high.fill(0);
        QPainter painter(&high);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        for (const auto& point : radio.points) {
            if (point.role != RadioRole::Tx || !std::isfinite(point.east_m) ||
                    !std::isfinite(point.north_m)) {
                continue;
            }
            const auto pixel = pixelBoundary(point.east_m, point.north_m, frame) * factor;
            painter.drawEllipse(pixel, 0.55 * factor, 0.55 * factor);
        }
        painter.end();
        txPresentation = downsample(high, txMask.size(), options.resampling_filter);
    }

    std::vector<double> sum(static_cast<std::size_t>(frame.width_px) * frame.height_px, 0.0);
    std::vector<unsigned> count(sum.size(), 0);
    for (const auto& point : radio.points) {
        if (point.role != RadioRole::Rx || !point.valid || !std::isfinite(point.power_dbfs) ||
                !std::isfinite(point.east_m) || !std::isfinite(point.north_m)) {
            continue;
        }
        const auto pixel = pixelBoundary(point.east_m, point.north_m, frame);
        const int x = static_cast<int>(std::floor(pixel.x()));
        const int y = static_cast<int>(std::floor(pixel.y()));
        if (x < 0 || y < 0 || x >= frame.width_px || y >= frame.height_px) {
            continue;
        }
        const auto index = static_cast<std::size_t>(y) * frame.width_px + x;
        sum[index] += std::pow(10.0, point.power_dbfs / 10.0);
        ++count[index];
    }
    for (int y = 0; y < frame.height_px; ++y) {
        for (int x = 0; x < frame.width_px; ++x) {
            if (buildingMask.constScanLine(y)[x] != 0) {
                count[static_cast<std::size_t>(y) * frame.width_px + x] = 0;
            }
        }
    }
    unsigned maxCount = 1;
    for (const auto value : count) {
        maxCount = std::max(maxCount, value);
    }
    bundle.max_sample_count = maxCount;

    bundle.radio_grid.width_px = frame.width_px;
    bundle.radio_grid.height_px = frame.height_px;
    bundle.radio_grid.meters_per_pixel = frame.meters_per_pixel;
    bundle.radio_grid.power_dbfs.assign(sum.size(), std::numeric_limits<double>::quiet_NaN());
    bundle.radio_grid.sample_count = count;
    bundle.radio_grid.building_mask.assign(sum.size(), 0);
    QImage countGray(frame.width_px, frame.height_px, QImage::Format_Grayscale8);
    for (int y = 0; y < frame.height_px; ++y) {
        for (int x = 0; x < frame.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * frame.width_px + x;
            const bool insideBuilding = buildingMask.constScanLine(y)[x] != 0;
            bundle.radio_grid.building_mask[index] = insideBuilding ? 1 : 0;
            if (!insideBuilding && count[index] > 0) {
                bundle.radio_grid.power_dbfs[index] = 10.0 * std::log10(sum[index] / count[index]);
            }
            // countGray.scanLine(y)[x] =
            //     insideBuilding ? 0 : static_cast<uchar>(std::lround(255.0 * count[index] / maxCount));
            countGray.scanLine(y)[x] = !insideBuilding && count[index] > 0 ? 255 : 0;
        }
    }
    bundle.building = colorize(buildingPresentation, options.color_mode, QColor(245, 170, 35));
    bundle.tx = colorize(txPresentation, options.color_mode, QColor(220, 40, 160));
    bundle.radio = renderRadioGrid(bundle.radio_grid, bundle.radio_grid.power_dbfs, defaultPower,
                                   powerMin, powerMax, options.color_mode);
    // bundle.count = colorize(countGray, options.color_mode, QColor());
    bundle.count = countGray;
    bundle.metadata = {
        {"format", "B210RadioMapPost raster frame v2"},
        {"min_local_x_m", frame.min_east_m},
        {"max_local_x_m", frame.max_east_m},
        {"min_local_y_m", frame.min_north_m},
        {"max_local_y_m", frame.max_north_m},
        {"min_east_m", frame.min_east_m},
        {"max_east_m", frame.max_east_m},
        {"min_north_m", frame.min_north_m},
        {"max_north_m", frame.max_north_m},
        {"legacy_bound_names_are_raster_local", true},
        {"meters_per_pixel", frame.meters_per_pixel},
        {"width_px", frame.width_px},
        {"height_px", frame.height_px},
        {"rotation_deg_ccw", frame.rotation_deg},
        {"coordinate_system", "rotated raster-local ENU"},
        {"north_up", std::abs(frame.rotation_deg) < 1e-12},
        {"raster_local_y_up", true},
        {"default_power_dbfs", defaultPower},
        {"power_min_dbfs", powerMin},
        {"power_max_dbfs", powerMax},
        {"aggregation", "linear_power_mean"},
        {"aggregation_role", "RX only"},
        {"building_mask_stage", "after aggregation"},
        {"color_mode", options.color_mode == RasterColorMode::Color ? "color" : "grayscale"},
        {"smooth_export", options.smooth_export},
        {"smooth_scope", "geometry_only_no_scalar_interpolation"},
        {"supersampling_factor", options.smooth_export ? options.supersampling_factor : 1},
        {
            "resampling_filter",
            options.resampling_filter == RasterResamplingFilter::Bicubic ? "bicubic" : "bilinear"
        },
        {"visible_flags_affect_raster", false},
        {"max_sample_count", static_cast<int>(maxCount)}
    };
    int measuredCells = 0;
    int missingCells = 0;
    int buildingCells = 0;
    for (std::size_t index = 0; index < bundle.radio_grid.cellCount(); ++index) {
        if (bundle.radio_grid.building_mask[index]) {
            ++buildingCells;
        } else if (bundle.radio_grid.sample_count[index] > 0) {
            ++measuredCells;
        } else {
            ++missingCells;
        }
    }
    bundle.metadata["measured_raster_cells"] = measuredCells;
    bundle.metadata["missing_free_space_cells"] = missingCells;
    bundle.metadata["building_raster_cells"] = buildingCells;
    return bundle;
}

QImage RasterRenderer::renderRadioGrid(const RadioRasterGrid& grid,
                                       const std::vector<double>& displayedPowerDbfs,
                                       double defaultPower, double powerMin, double powerMax,
                                       RasterColorMode colorMode) {
    if (grid.width_px <= 0 || grid.height_px <= 0 || grid.cellCount() != displayedPowerDbfs.size() ||
            grid.cellCount() != grid.building_mask.size()) {
        return {};
    }
    QImage grayscale(grid.width_px, grid.height_px, QImage::Format_Grayscale8);
    for (int y = 0; y < grid.height_px; ++y) {
        auto* line = grayscale.scanLine(y);
        for (int x = 0; x < grid.width_px; ++x) {
            const auto index = static_cast<std::size_t>(y) * grid.width_px + x;
            const double power =
                grid.building_mask[index] == 0 && std::isfinite(displayedPowerDbfs[index])
                ? displayedPowerDbfs[index]
                : defaultPower;
            const double normalized =
                std::clamp((power - powerMin) / std::max(1e-9, powerMax - powerMin), 0.0, 1.0);
            line[x] = static_cast<uchar>(std::lround(255.0 * normalized));
        }
    }
    return colorize(grayscale, colorMode, QColor());
}

QImage RasterRenderer::addColorbar(const QImage& raster, double minimum, double maximum,
                                   const QString& title, RasterColorMode colorMode) {
    if (raster.isNull()) {
        return {};
    }
    const int outputHeight = std::max(raster.height(), 140);
    const int marginWidth = 150;
    QImage output(raster.width() + marginWidth, outputHeight, QImage::Format_ARGB32);
    output.fill(Qt::white);
    QPainter painter(&output);
    painter.drawImage(0, 0, raster);
    const int barTop = 30;
    const int barBottom = outputHeight - 30;
    const QRectF bar(raster.width() + 24, barTop, 22, std::max(20, barBottom - barTop));
    QLinearGradient gradient(bar.bottomLeft(), bar.topLeft());
    if (colorMode == RasterColorMode::Color) {
        for (int step = 0; step <= 20; ++step) {
            const double normalized = step / 20.0;
            gradient.setColorAt(normalized, scalarColor(qRound(255.0 * normalized)));
        }
    } else {
        gradient.setColorAt(0.0, Qt::black);
        gradient.setColorAt(1.0, Qt::white);
    }
    painter.fillRect(bar, gradient);
    painter.setPen(Qt::black);
    painter.drawRect(bar);
    painter.drawText(QPointF(bar.left(), 18), title);
    painter.drawText(QPointF(bar.right() + 8, bar.top() + 5), QString::number(maximum, 'f', 2));
    painter.drawText(QPointF(bar.right() + 8, bar.bottom()), QString::number(minimum, 'f', 2));
    return output;
}

QString RasterRenderer::alternateRadioMapFileName(const QString& primaryFileName,
        RasterColorMode alternateMode) {
    const QFileInfo info(primaryFileName);
    QString baseName = info.completeBaseName();
    const QString sourceToken = alternateMode == RasterColorMode::Color
                                ? QStringLiteral("gray")
                                : QStringLiteral("rgb");
    const QString targetToken = alternateMode == RasterColorMode::Color
                                ? QStringLiteral("rgb")
                                : QStringLiteral("gray");
    if (baseName.contains(sourceToken, Qt::CaseInsensitive)) {
        baseName.replace(sourceToken, targetToken, Qt::CaseInsensitive);
    } else {
        baseName += QStringLiteral("_") + targetToken;
    }
    return info.suffix().isEmpty() ? baseName : baseName + QStringLiteral(".") + info.suffix();
}

bool RasterRenderer::saveMetadata(const QString& path, const QJsonObject& o, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace b210post
