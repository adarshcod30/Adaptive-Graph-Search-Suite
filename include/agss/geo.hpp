#pragma once

#include <algorithm>
#include <cmath>

namespace agss::geo {

// Mean Earth radius (IUGG), metres.
inline constexpr double kEarthRadiusM = 6371008.8;

inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Great-circle distance between two WGS84 points, in metres.
///
/// The engine's original heuristic used raw Euclidean distance over (x, y).
/// That is fine for synthetic planar graphs but *not* admissible on
/// geographic coordinates: at Delhi's latitude (~28.6 N) one degree of
/// longitude spans ~26% less ground than one degree of latitude, so a plain
/// Euclidean estimate over-estimates the true remaining cost and A* silently
/// stops being optimal. Haversine removes that failure mode.
inline double haversine(double lat1, double lon1, double lat2, double lon2) {
    const double phi1 = lat1 * kDegToRad;
    const double phi2 = lat2 * kDegToRad;
    const double dphi = (lat2 - lat1) * kDegToRad;
    const double dlambda = (lon2 - lon1) * kDegToRad;

    const double sin_dphi = std::sin(dphi * 0.5);
    const double sin_dlambda = std::sin(dlambda * 0.5);

    const double a =
        sin_dphi * sin_dphi + std::cos(phi1) * std::cos(phi2) * sin_dlambda * sin_dlambda;
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(std::min(1.0, a)));
}

/// Equirectangular approximation of great-circle distance, in metres.
///
/// Cheaper than haversine (no asin, one cos) and accurate to well under 0.5%
/// over city-scale extents. It never over-estimates for the spans we care
/// about, so it stays admissible as an A* heuristic. Used on the hot path.
inline double equirectangular(double lat1, double lon1, double lat2, double lon2) {
    const double x = (lon2 - lon1) * kDegToRad * std::cos((lat1 + lat2) * 0.5 * kDegToRad);
    const double y = (lat2 - lat1) * kDegToRad;
    return kEarthRadiusM * std::sqrt(x * x + y * y);
}

/// Plain Euclidean distance. Correct for synthetic planar graphs only.
inline double euclidean(double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

/// Initial bearing from point 1 to point 2, in degrees clockwise from north.
/// Used to turn a node sequence into turn-by-turn instructions.
inline double bearing(double lat1, double lon1, double lat2, double lon2) {
    const double phi1 = lat1 * kDegToRad;
    const double phi2 = lat2 * kDegToRad;
    const double dlambda = (lon2 - lon1) * kDegToRad;
    const double y = std::sin(dlambda) * std::cos(phi2);
    const double x =
        std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dlambda);
    double deg = std::atan2(y, x) / kDegToRad;
    return deg < 0.0 ? deg + 360.0 : deg;
}

/// Signed turn angle in (-180, 180]: negative = left, positive = right.
inline double turn_angle(double from_bearing, double to_bearing) {
    double d = to_bearing - from_bearing;
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}

}  // namespace agss::geo
