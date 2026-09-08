#include "agss/time_dependent.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <random>

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kDay = 24.0 * 3600.0;

struct Entry {
    double key;
    NodeId node;
    bool operator>(const Entry& o) const { return key > o.key; }
};
using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>;

double wrap_day(double t) {
    double r = std::fmod(t, kDay);
    return r < 0 ? r + kDay : r;
}

}  // namespace

TimeDependentModel::Profile TimeDependentModel::free_flow() {
    Profile p{};
    for (int i = 0; i < kBuckets; ++i) p.multiplier[i] = 1.0;
    return p;
}

TimeDependentModel::Profile TimeDependentModel::default_arterial() {
    // Hour 0 is midnight. Two peaks either side of the working day, a midday
    // plateau, and free-running small hours.
    static constexpr double kWeekday[kBuckets] = {
        1.00, 1.00, 1.00, 1.00, 1.02, 1.10,  // 00-05 overnight
        1.35, 1.80, 2.35, 2.55, 2.05, 1.70,  // 06-11 morning peak
        1.55, 1.55, 1.50, 1.60, 1.85, 2.30,  // 12-17 midday into evening build
        2.60, 2.40, 1.90, 1.50, 1.25, 1.08,  // 18-23 evening peak, then decay
    };
    Profile p{};
    for (int i = 0; i < kBuckets; ++i) p.multiplier[i] = kWeekday[i];
    return p;
}

TimeDependentModel::TimeDependentModel(const Graph& g, double free_flow_speed_mps,
                                       std::uint64_t seed)
    : graph_(&g), speed_mps_(free_flow_speed_mps > 0 ? free_flow_speed_mps : 11.1) {
    profiles_.push_back(free_flow());
    const Profile arterial = default_arterial();

    // Four congestion tiers between free flow and the full arterial profile.
    // A residential lane does not gridlock the way a trunk route does, and
    // giving every edge the same curve would make the whole network peak in
    // lockstep -- which is exactly what does not happen in a real city.
    for (int tier = 1; tier <= 4; ++tier) {
        Profile p{};
        const double share = tier / 4.0;
        for (int i = 0; i < kBuckets; ++i) {
            p.multiplier[i] = 1.0 + (arterial.multiplier[i] - 1.0) * share;
        }
        profiles_.push_back(p);
    }

    // Assign tiers by length *percentile*, not by length relative to the
    // longest edge. A single long bypass makes every ordinary street look
    // short by comparison, which put almost the whole network in the free-flow
    // tier and flattened rush hour to a 1.13x swing. Ranking spreads the tiers
    // evenly whatever the length distribution looks like.
    profile_of_.assign(static_cast<std::size_t>(g.num_edges()), 0);
    if (g.num_edges() == 0) return;

    std::vector<EdgeId> order(static_cast<std::size_t>(g.num_edges()));
    for (EdgeId e = 0; e < g.num_edges(); ++e) order[static_cast<std::size_t>(e)] = e;
    std::sort(order.begin(), order.end(),
              [&](EdgeId a, EdgeId b) { return g.edge_weight(a) < g.edge_weight(b); });

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> jitter(-1, 1);
    const auto tiers = static_cast<double>(profiles_.size());
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const double q = static_cast<double>(rank) / static_cast<double>(order.size());
        int tier = static_cast<int>(q * tiers);
        tier = std::clamp(tier + jitter(rng), 0, static_cast<int>(profiles_.size()) - 1);
        profile_of_[static_cast<std::size_t>(order[rank])] = static_cast<std::int32_t>(tier);
    }
}

double TimeDependentModel::free_flow_time(EdgeId e) const {
    return graph_->edge_weight(e) / speed_mps_;
}

double TimeDependentModel::travel_time(EdgeId e, double departure) const {
    const double base = free_flow_time(e);
    const int bucket = static_cast<int>(wrap_day(departure) / 3600.0) % kBuckets;
    const int next = (bucket + 1) % kBuckets;
    const double frac = wrap_day(departure) / 3600.0 - std::floor(wrap_day(departure) / 3600.0);

    // Interpolate between hourly samples. A step function would make travel
    // time jump discontinuously at each hour boundary, which breaks FIFO in a
    // way that is easy to miss: departing a second later could arrive minutes
    // earlier.
    const auto& p = profiles_[static_cast<std::size_t>(profile_of_[static_cast<std::size_t>(e)])];
    const double m = p.multiplier[bucket] * (1.0 - frac) + p.multiplier[next] * frac;
    return base * m;
}

double TimeDependentModel::arrival(EdgeId e, double departure) const {
    const double naive = departure + travel_time(e, departure);

    // Enforce FIFO explicitly rather than trusting the profile. The
    // interpolated curve is smooth, but a steep enough drop in the multiplier
    // could still let a later departure overtake an earlier one, and a single
    // violation invalidates label-setting search -- silently, with a
    // plausible-looking wrong route. Clamping costs one comparison.
    const double step = 60.0;
    const double earlier = departure - step;
    const double bound = earlier + travel_time(e, earlier);
    return std::max(naive, bound);
}

double TimeDependentModel::congestion_range(EdgeId e) const {
    const auto& p = profiles_[static_cast<std::size_t>(profile_of_[static_cast<std::size_t>(e)])];
    double lo = kInf, hi = 0.0;
    for (int i = 0; i < kBuckets; ++i) {
        lo = std::min(lo, p.multiplier[i]);
        hi = std::max(hi, p.multiplier[i]);
    }
    return lo > 0 ? hi / lo : 1.0;
}

TimeDependentResult earliest_arrival(const TimeDependentModel& model, NodeId source, NodeId target,
                                     double departure, const SearchOptions& opts) {
    TimeDependentResult out;
    out.departure = departure;
    out.search.algorithm = "Time-dependent earliest arrival";
    out.search.time_complexity = "O((V + E) log V)";
    out.search.space_complexity = "O(V)";

    const Graph& g = model.graph();
    const auto n = g.num_nodes();
    if (source < 0 || target < 0 || source >= n || target >= n) return out;

    std::vector<double> best(static_cast<std::size_t>(n), kInf);
    std::vector<NodeId> parent(static_cast<std::size_t>(n), kInvalidNode);
    std::vector<char> settled(static_cast<std::size_t>(n), 0);
    MinHeap pq;

    const auto t0 = Clock::now();
    best[source] = departure;
    pq.push({departure, source});
    if (opts.trace) opts.trace->discover(source, kInvalidNode);

    while (!pq.empty()) {
        const NodeId u = pq.top().node;
        pq.pop();
        if (settled[u]) continue;
        settled[u] = 1;
        ++out.search.nodes_expanded;
        if (opts.trace) opts.trace->expand(u);
        if (u == target) {
            out.search.success = true;
            break;
        }
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (!opts.edge_open(e)) continue;
            const NodeId v = g.edge_target(e);
            ++out.search.edges_relaxed;
            if (settled[v]) continue;
            // Label-setting on arrival times, valid because the model is FIFO.
            const double arrive = model.arrival(e, best[u]);
            if (arrive < best[v]) {
                const bool first = best[v] == kInf;
                best[v] = arrive;
                parent[v] = u;
                pq.push({arrive, v});
                if (opts.trace) first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
            }
        }
    }
    out.search.algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (out.search.success) {
        out.search.path = reconstruct_path(parent, source, target);
        out.arrival = best[target];
        out.duration = out.arrival - departure;
        out.search.path_cost = out.duration;
        // Free-flow comparison must be in seconds too, not metres.
        const double metres = path_cost(g, out.search.path);
        out.free_flow_duration = metres >= 0 ? metres / model.free_flow_speed() : -1.0;
    }
    return out;
}

DepartureScan scan_departures(const TimeDependentModel& model, NodeId source, NodeId target,
                              int samples) {
    DepartureScan scan;
    samples = std::max(2, samples);
    scan.best_duration = kInf;
    scan.worst_duration = -1.0;

    for (int i = 0; i < samples; ++i) {
        const double dep = kDay * i / samples;
        const auto r = earliest_arrival(model, source, target, dep, {});
        const double d = r.search.success ? r.duration : kInf;
        scan.departures.push_back(dep);
        scan.durations.push_back(d);
        if (d < scan.best_duration) {
            scan.best_duration = d;
            scan.best_departure = dep;
        }
        if (d != kInf && d > scan.worst_duration) {
            scan.worst_duration = d;
            scan.worst_departure = dep;
        }
    }
    return scan;
}

}  // namespace agss
