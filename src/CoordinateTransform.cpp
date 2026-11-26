#include "CoordinateTransform.hpp"

#include <cmath>
#include <stdexcept>

namespace b210post {
namespace {
constexpr double pi = 3.14159265358979323846, a = 6378137.0, f = 1.0 / 298.257223563,
                 e2 = f * (2.0 - f);

double rad(double d) {
    return d * pi / 180.0;
}

double deg(double r) {
    return r * 180.0 / pi;
}

struct Ecef {
    double x, y, z;
};

Ecef ecef(double la, double lo, double h) {
    double p = rad(la), l = rad(lo), s = std::sin(p), c = std::cos(p),
           n = a / std::sqrt(1 - e2 * s * s);
    return {(n + h) * c * std::cos(l), (n + h) * c * std::sin(l), (n * (1 - e2) + h) * s};
}

Wgs84Coordinate geodetic(Ecef p) {
    double lo = std::atan2(p.y, p.x), rho = std::hypot(p.x, p.y),
           la = std::atan2(p.z, rho * (1 - e2)), h = 0;
    for (int i = 0; i < 12; ++i) {
        double s = std::sin(la), n = a / std::sqrt(1 - e2 * s * s), c = std::cos(la);
        h = std::abs(c) > 1e-12 ? rho / c - n : std::abs(p.z) - n * (1 - e2);
        double q = std::atan2(p.z, rho * (1 - e2 * n / (n + h)));
        if (std::abs(q - la) < 1e-14) {
            la = q;
            break;
        }
        la = q;
    }
    double s = std::sin(la), n = a / std::sqrt(1 - e2 * s * s), c = std::cos(la);
    if (std::abs(c) > 1e-12)
        h = rho / c - n;
    return {deg(la), deg(lo), h};
}

}  // namespace
}  // namespace b210post

namespace b210post {
EnuCoordinate CoordinateTransform::wgs84ToEnu(double la, double lo, double h,
        const EnuReference& r) {
    if (!r.valid)
        throw std::invalid_argument("invalid ENU reference");
    if (!std::isfinite(h))
        h = r.altitude_m;
    auto p = ecef(la, lo, h), o = ecef(r.latitude_deg, r.longitude_deg, r.altitude_m);
    double dx = p.x - o.x, dy = p.y - o.y, dz = p.z - o.z, P = rad(r.latitude_deg),
           L = rad(r.longitude_deg), sp = std::sin(P), cp = std::cos(P), sl = std::sin(L),
           cl = std::cos(L);
    return {-sl * dx + cl * dy, -sp * cl * dx - sp * sl * dy + cp * dz,
            cp * cl * dx + cp * sl * dy + sp * dz};
}

Wgs84Coordinate CoordinateTransform::enuToWgs84(double e, double n, double u,
        const EnuReference& r) {
    if (!r.valid)
        throw std::invalid_argument("invalid ENU reference");
    auto o = ecef(r.latitude_deg, r.longitude_deg, r.altitude_m);
    double P = rad(r.latitude_deg), L = rad(r.longitude_deg), sp = std::sin(P), cp = std::cos(P),
           sl = std::sin(L), cl = std::cos(L);
    double dx = -sl * e - sp * cl * n + cp * cl * u, dy = cl * e - sp * sl * n + cp * sl * u,
           dz = cp * n + sp * u;
    return geodetic({o.x + dx, o.y + dy, o.z + dz});
}

}  // namespace b210post
