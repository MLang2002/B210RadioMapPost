#ifndef __B210_COORDINATE_TRANSFORM_HPP
#define __B210_COORDINATE_TRANSFORM_HPP
#include "Types.hpp"

namespace b210post {
class CoordinateTransform {
 public:
  static EnuCoordinate wgs84ToEnu(double lat, double lon, double alt, const EnuReference& ref);
  static Wgs84Coordinate enuToWgs84(double east, double north, double up, const EnuReference& ref);
};
}  // namespace b210post

#endif // __B210_COORDINATE_TRANSFORM_HPP
