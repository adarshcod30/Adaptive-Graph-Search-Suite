// Time-dependent routing is only correct if the model is FIFO: leaving later
// must never let you arrive earlier. A single violation invalidates
// label-setting search, silently, with a plausible-looking wrong route.
#include <cmath>
#include <random>

#include "agss/time_dependent.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

TEST("timedep", "arrival is non-decreasing in departure (FIFO)") {
    const auto g = testing::unit_grid(14, 14);
    TimeDependentModel m(g);

    std::mt19937_64 rng(5);
    std::uniform_int_distribution<EdgeId> pick_edge(0, g.num_edges() - 1);
    for (int i = 0; i < 400; ++i) {
        const EdgeId e = pick_edge(rng);
        double prev = -1.0;
        // Sweep a whole day in five-minute steps.
        for (double t = 0; t < 24 * 3600; t += 300) {
            const double a = m.arrival(e, t);
            CHECK_MSG(a >= prev - 1e-9, "edge " << e << ": leaving at " << t << " arrives " << a
                                                << ", earlier than the previous departure's "
                                                << prev);
            CHECK(a >= t);  // you cannot arrive before you leave
            prev = a;
        }
    }
}

TEST("timedep", "travel time tracks the congestion curve") {
    const auto g = testing::unit_grid(12, 12);
    TimeDependentModel m(g);

    // Find an edge that actually carries a congestion profile.
    EdgeId busy = -1;
    for (EdgeId e = 0; e < g.num_edges(); ++e) {
        if (m.congestion_range(e) > 1.5) {
            busy = e;
            break;
        }
    }
    CHECK(busy >= 0);

    const double at_0300 = m.travel_time(busy, 3 * 3600.0);
    const double at_0900 = m.travel_time(busy, 9 * 3600.0);
    const double at_1800 = m.travel_time(busy, 18 * 3600.0);
    CHECK_MSG(at_0900 > at_0300, "morning peak should be slower than 03:00");
    CHECK_MSG(at_1800 > at_0300, "evening peak should be slower than 03:00");
    CHECK(m.free_flow_time(busy) <= at_0300 + 1e-9);
}

TEST("timedep", "the day wraps continuously") {
    const auto g = testing::unit_grid(8, 8);
    TimeDependentModel m(g);
    // 23:59:59 and 00:00:01 must be almost the same conditions, not a cliff.
    const double before = m.travel_time(0, 24 * 3600.0 - 1);
    const double after = m.travel_time(0, 1.0);
    CHECK_MSG(std::abs(before - after) / std::max(1.0, before) < 0.02,
              "midnight discontinuity: " << before << " vs " << after);
    // Negative and beyond-a-day inputs must still land somewhere sane.
    CHECK(m.travel_time(0, -3600.0) > 0);
    CHECK(m.travel_time(0, 3 * 24 * 3600.0 + 7200.0) > 0);
}

TEST("timedep", "at free flow it agrees with Dijkstra on distance") {
    // With speed fixed and congestion at its overnight minimum, the fastest
    // route should be the shortest one, and the duration should be the
    // distance divided by the speed.
    const auto g = testing::unit_grid(15, 15);
    const double speed = 10.0;
    TimeDependentModel m(g, speed);
    auto dij = Registry::instance().create("dijkstra");

    std::mt19937_64 rng(9);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    int checked = 0;
    for (int i = 0; i < 40; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto want = dij->run(g, s, t, {});
        const auto got = earliest_arrival(m, s, t, 2 * 3600.0);  // 02:00, quiet
        CHECK_EQ(got.search.success, want.success);
        if (!want.success) continue;
        // Overnight multipliers are 1.0, so duration is distance / speed.
        CHECK_NEAR(got.duration, want.path_cost / speed, 1e-6);
        CHECK(path_cost(g, got.search.path) >= 0.0);
        ++checked;
    }
    CHECK(checked > 15);
}

TEST("timedep", "rush hour is slower than the small hours") {
    const auto g = testing::unit_grid(20, 20);
    TimeDependentModel m(g);
    const auto quiet = earliest_arrival(m, 0, 399, 3 * 3600.0);
    const auto peak = earliest_arrival(m, 0, 399, 18 * 3600.0);
    CHECK(quiet.search.success && peak.search.success);
    CHECK_MSG(peak.duration > quiet.duration * 1.2,
              "peak " << peak.duration << " vs quiet " << quiet.duration);
    // The trip never beats free flow.
    CHECK(quiet.duration >= quiet.free_flow_duration - 1e-6);
}

TEST("timedep", "a departure scan finds both peaks") {
    const auto g = testing::unit_grid(20, 20);
    TimeDependentModel m(g);
    const auto scan = scan_departures(m, 0, 399, 24);
    CHECK_EQ(scan.departures.size(), std::size_t{24});
    CHECK(scan.best_duration < scan.worst_duration);
    // The quietest departure should be overnight, the busiest during a peak.
    const int best_hour = static_cast<int>(scan.best_departure / 3600);
    const int worst_hour = static_cast<int>(scan.worst_departure / 3600);
    CHECK_MSG(best_hour <= 5 || best_hour >= 22, "quietest hour was " << best_hour);
    CHECK_MSG((worst_hour >= 7 && worst_hour <= 10) || (worst_hour >= 16 && worst_hour <= 19),
              "busiest hour was " << worst_hour);
}

TEST("timedep", "departing later never arrives earlier, end to end") {
    // The edge-level FIFO check above is necessary but not sufficient: the
    // property has to survive composition along a whole route.
    const auto g = testing::unit_grid(16, 16);
    TimeDependentModel m(g);
    double prev_arrival = -1.0;
    for (double dep = 0; dep < 24 * 3600; dep += 900) {
        const auto r = earliest_arrival(m, 0, 255, dep);
        CHECK(r.search.success);
        CHECK_MSG(r.arrival >= prev_arrival - 1e-6,
                  "departing at " << dep << " arrives " << r.arrival
                                  << ", before the previous departure's " << prev_arrival);
        prev_arrival = r.arrival;
    }
}

TEST("timedep", "degenerate inputs") {
    const auto g = testing::unit_grid(6, 6);
    TimeDependentModel m(g);
    CHECK(!earliest_arrival(m, -1, 5, 0).search.success);
    CHECK(!earliest_arrival(m, 0, 9999, 0).search.success);
    const auto self = earliest_arrival(m, 7, 7, 3600.0);
    CHECK(self.search.success);
    CHECK_NEAR(self.duration, 0.0, 1e-9);

    GraphBuilder eb;
    const auto empty = eb.build();
    TimeDependentModel em(empty);
    CHECK(!earliest_arrival(em, 0, 0, 0).search.success);
}
