#ifndef __B210_INTERPOLATION_HPP
#define __B210_INTERPOLATION_HPP

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <cstdint>
#include <memory>
#include <vector>

#include "Types.hpp"

namespace b210post {

class InterpolationParameterWidget : public QWidget {
  public:
    using QWidget::QWidget;
    ~InterpolationParameterWidget() override = default;

    virtual QJsonObject parameters() const = 0;
};

struct InterpolationRequest {
    const RadioDataset* radio = nullptr;
    const BuildingDataset* building = nullptr;
    QRectF region;
    QJsonObject parameters;
};

struct InterpolatedSample {
    QPointF position;
    double power_dbfs = 0.0;
    double uncertainty = 0.0;
};

struct InterpolationResult {
    std::vector<InterpolatedSample> samples;
    QStringList warnings;
    int candidate_count = 0;
    int skipped_near_existing = 0;
    int skipped_building = 0;
    int skipped_support = 0;
    int skipped_barrier = 0;
    int skipped_uncertainty = 0;
};

class InterpolationMethod {
  public:
    virtual ~InterpolationMethod() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const = 0;
    virtual std::unique_ptr<InterpolationParameterWidget> createParameterWidget(
        QWidget* parent) const = 0;
    virtual bool validateParameters(const QJsonObject& parameters, QString* error) const = 0;
    virtual InterpolationResult interpolate(const InterpolationRequest& request) const = 0;
};

std::vector<std::unique_ptr<InterpolationMethod>> createBuiltInInterpolationMethods();

}  // namespace b210post

#endif // __B210_INTERPOLATION_HPP
