#pragma once

#include <string>
#include <vector>

#include "agss/graph.hpp"

namespace agss {

enum class Maneuver {
    Depart,
    Straight,
    SlightLeft,
    Left,
    SharpLeft,
    SlightRight,
    Right,
    SharpRight,
    UTurn,
    Arrive
};

struct Step {
    Maneuver maneuver = Maneuver::Straight;
    NodeId at = kInvalidNode;
    double distance = 0.0;  ///< distance of the leg that follows this step
    double bearing = 0.0;   ///< outgoing bearing, degrees clockwise from north
    double turn = 0.0;      ///< signed turn angle, negative = left
    std::string text;
};

struct Directions {
    std::vector<Step> steps;
    double total_distance = 0.0;
};

std::string to_string(Maneuver m);

/// Turn a node sequence into human-readable instructions.
///
/// Consecutive hops that continue roughly straight are merged into one leg, so
/// a 40-node path across a city becomes a handful of instructions rather than
/// 40 "continue straight" lines. Bearings are true bearings on geographic
/// graphs and planar angles otherwise.
Directions build_directions(const Graph& g, const std::vector<NodeId>& path,
                            double straight_threshold_deg = 20.0);

}  // namespace agss
