#ifndef __B210_APP_DEFAULTS_HPP
#define __B210_APP_DEFAULTS_HPP

namespace b210post::defaults {

constexpr double kAnonymousOriginLatitudeDeg = 0.0;
constexpr double kAnonymousOriginLongitudeDeg = 0.0;
constexpr double kAnonymousOriginAltitudeM = 10.0;
constexpr double kDefaultPowerDbfs = -120.0;
constexpr double kPowerColorMinDbfs = -120.0;
constexpr double kPowerColorMaxDbfs = -20.0;
constexpr double kMetersPerPixel = 1.0;
constexpr double kRasterLeftM = 50.0;
constexpr double kRasterRightM = 50.0;
constexpr double kRasterBottomM = 50.0;
constexpr double kRasterTopM = 50.0;
constexpr double kRasterRotationDeg = 0.0;
constexpr int kRasterSupersamplingFactor = 4;
constexpr int kPointSizePx = 6;
constexpr double kInactiveOpacity = 0.22;

}  // namespace b210post::defaults

#endif  // __B210_APP_DEFAULTS_HPP