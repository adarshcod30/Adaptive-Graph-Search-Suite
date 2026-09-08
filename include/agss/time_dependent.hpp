#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

/// Routing where an edge's cost depends on when you enter it.
///
/// A road network is not a static graph. Outer Ring Road at 09:00 and at 02:00
/// are different graphs with the same shape, and the fastest route changes
/// accordingly. This models each edge as a free-flow travel time scaled by a
/// congestion profile sampled through the day.
///
/// The critical property is **FIFO** (also called non-overtaking): leaving
/// later can never get you there earlier. Real traffic satisfies it -- you
/// cannot overtake yourself by departing later -- and it is what keeps plain
/// Dijkstra correct on a time-dependent graph. Without it the problem becomes
/// NP-hard in general, and a label-setting search stops being valid. The
/// profile below is clamped to guarantee it.
class TimeDependentModel {
public:
    /// Congestion samples across 24 hours, as multipliers on free-flow time.
    /// 1.0 means free flow; 2.0 means the trip takes twice as long.
    static constexpr int kBuckets = 24;

    struct Profile {
        double multiplier[kBuckets];
    };

    /// Weekday traffic on an Indian arterial: two sharp peaks, a midday lull
    /// and a fast overnight window. Applied in proportion to how major the
    /// road is, since a residential lane does not gridlock the way a trunk
    /// route does.
    static Profile default_arterial();
    static Profile free_flow();

    /// Build a model over `g`, assigning each edge a congestion profile.
    ///
    /// Edge weights are read as **metres** and converted to seconds at
    /// `free_flow_speed_mps`. Conflating the two units is an easy mistake with
    /// no type to catch it: a first pass here treated metres as seconds and
    /// reported 150 minutes to cross Bengaluru.
    /// `seed` makes the congestion-tier assignment reproducible.
    TimeDependentModel(const Graph& g, double free_flow_speed_mps = 11.1,
                       std::uint64_t seed = 20260908);

    double free_flow_speed() const noexcept { return speed_mps_; }

    /// Free-flow seconds along `e`, ignoring congestion.
    double free_flow_time(EdgeId e) const;

    /// Travel time along edge `e` when entering it at `departure` seconds
    /// after midnight.
    double travel_time(EdgeId e, double departure) const;

    /// Arrival time at the far end of `e`, entering at `departure`.
    /// Guaranteed non-decreasing in `departure`, which is the FIFO property.
    double arrival(EdgeId e, double departure) const;

    const Graph& graph() const noexcept { return *graph_; }

    /// Ratio of the slowest to the fastest departure time for this edge.
    double congestion_range(EdgeId e) const;

private:
    const Graph* graph_;
    double speed_mps_ = 11.1;
    std::vector<std::int32_t> profile_of_;  // edge -> index into profiles_
    std::vector<Profile> profiles_;
};

struct TimeDependentResult {
    SearchResult search;
    double departure = 0.0;  ///< seconds after midnight
    double arrival = 0.0;
    double duration = 0.0;  ///< arrival - departure, seconds
    /// What the same route would have cost at free flow, for comparison.
    double free_flow_duration = 0.0;
};

/// Earliest-arrival query: leave `source` at `departure` and reach `target` as
/// soon as possible. Correct with a plain label-setting search because the
/// model is FIFO.
TimeDependentResult earliest_arrival(const TimeDependentModel& model, NodeId source, NodeId target,
                                     double departure, const SearchOptions& opts = {});

/// Sweep departure times across the day and report the duration of each, which
/// is how you see rush hour rather than assert it.
struct DepartureScan {
    std::vector<double> departures;  ///< seconds after midnight
    std::vector<double> durations;   ///< seconds; infinity where unreachable
    double best_departure = 0.0;
    double best_duration = 0.0;
    double worst_departure = 0.0;
    double worst_duration = 0.0;
};

DepartureScan scan_departures(const TimeDependentModel& model, NodeId source, NodeId target,
                              int samples = 24);

}  // namespace agss
