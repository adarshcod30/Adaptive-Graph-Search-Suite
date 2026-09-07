#include "agss/directions.hpp"

#include <cmath>

#include "agss/geo.hpp"

namespace agss {
namespace {

double leg_bearing(const Graph& g, NodeId a, NodeId b) {
    if (g.coord_space() == CoordSpace::Geographic) {
        return geo::bearing(g.lat(a), g.lon(a), g.lat(b), g.lon(b));
    }
    // Planar: measure clockwise from +y so the vocabulary matches compass use.
    const double deg = std::atan2(g.x(b) - g.x(a), g.y(b) - g.y(a)) / geo::kDegToRad;
    return deg < 0.0 ? deg + 360.0 : deg;
}

double leg_distance(const Graph& g, NodeId a, NodeId b) {
    return g.straight_line(a, b);
}

Maneuver classify(double turn, double straight_threshold) {
    const double a = std::abs(turn);
    if (a >= 150.0) return Maneuver::UTurn;
    if (a <= straight_threshold) return Maneuver::Straight;
    if (a <= 45.0) return turn < 0 ? Maneuver::SlightLeft : Maneuver::SlightRight;
    if (a <= 120.0) return turn < 0 ? Maneuver::Left : Maneuver::Right;
    return turn < 0 ? Maneuver::SharpLeft : Maneuver::SharpRight;
}

std::string format_distance(double d, bool geographic) {
    char buf[64];
    if (!geographic) {
        std::snprintf(buf, sizeof(buf), "%.1f units", d);
    } else if (d >= 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.1f km", d / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f m", d);
    }
    return buf;
}

}  // namespace

std::string to_string(Maneuver m) {
    // clang-format off
    switch (m) {
        case Maneuver::Depart:      return "depart";
        case Maneuver::Straight:    return "continue straight";
        case Maneuver::SlightLeft:  return "bear left";
        case Maneuver::Left:        return "turn left";
        case Maneuver::SharpLeft:   return "sharp left";
        case Maneuver::SlightRight: return "bear right";
        case Maneuver::Right:       return "turn right";
        case Maneuver::SharpRight:  return "sharp right";
        case Maneuver::UTurn:       return "make a U-turn";
        case Maneuver::Arrive:      return "arrive";
    }
    // clang-format on
    return "continue";
}

Directions build_directions(const Graph& g, const std::vector<NodeId>& path,
                            double straight_threshold_deg) {
    Directions out;
    if (path.size() < 2) return out;
    const bool geographic = g.coord_space() == CoordSpace::Geographic;

    double prev_bearing = leg_bearing(g, path[0], path[1]);
    Step depart;
    depart.maneuver = Maneuver::Depart;
    depart.at = path[0];
    depart.bearing = prev_bearing;
    out.steps.push_back(depart);

    double run = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const double d = leg_distance(g, path[i], path[i + 1]);
        run += d;
        out.total_distance += d;

        if (i + 2 >= path.size()) break;
        const double next_bearing = leg_bearing(g, path[i + 1], path[i + 2]);
        const double turn = geo::turn_angle(prev_bearing, next_bearing);
        const Maneuver m = classify(turn, straight_threshold_deg);

        // Merge straight-through hops into the running leg instead of
        // emitting an instruction per intersection.
        if (m == Maneuver::Straight) {
            prev_bearing = next_bearing;
            continue;
        }

        out.steps.back().distance = run;
        out.steps.back().text = to_string(out.steps.back().maneuver) + ", then continue for " +
                                format_distance(run, geographic);
        run = 0.0;

        Step s;
        s.maneuver = m;
        s.at = path[i + 1];
        s.bearing = next_bearing;
        s.turn = turn;
        out.steps.push_back(s);
        prev_bearing = next_bearing;
    }

    out.steps.back().distance = run;
    out.steps.back().text = to_string(out.steps.back().maneuver) + ", then continue for " +
                            format_distance(run, geographic);

    Step arrive;
    arrive.maneuver = Maneuver::Arrive;
    arrive.at = path.back();
    arrive.text = "arrive at destination";
    out.steps.push_back(arrive);
    return out;
}

}  // namespace agss
