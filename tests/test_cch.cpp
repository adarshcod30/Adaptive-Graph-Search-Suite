// CCH must answer exactly like Dijkstra for whatever metric it was last
// customized with, and re-customizing must be far cheaper than rebuilding.
#include <random>

#include "agss/customizable_ch.hpp"
#include "agss/time_dependent.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

namespace {

/// Compare against a Dijkstra run on a graph carrying the same metric.
void expect_matches(const Graph& metric_graph, const CustomizableCH& cch, int trials,
                    std::uint64_t seed, const char* label) {
    auto dij = Registry::instance().create("dijkstra");
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<NodeId> pick(0, metric_graph.num_nodes() - 1);
    int compared = 0;
    for (int i = 0; i < trials; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto want = dij->run(metric_graph, s, t, {});
        const auto got = cch.query(s, t, {});
        CHECK_MSG(got.success == want.success, label << ": reachability disagrees");
        if (!want.success) continue;
        CHECK_MSG(mt::close(got.path_cost, want.path_cost, 1e-9),
                  label << ": CCH=" << got.path_cost << " Dijkstra=" << want.path_cost << " for "
                        << s << "->" << t);
        CHECK_MSG(got.path.front() == s && got.path.back() == t,
                  label << ": unpacked path has the wrong endpoints");
        CHECK_MSG(path_cost(metric_graph, got.path) >= 0.0,
                  label << ": unpacked path contains a non-edge");
        ++compared;
    }
    CHECK(compared > trials / 3);
}

/// Rebuild a graph with the same shape but different weights.
Graph reweight(const Graph& g, double factor_seed) {
    GraphBuilder b(g.coord_space());
    for (NodeId v = 0; v < g.num_nodes(); ++v) b.add_node(v, g.x(v), g.y(v));
    std::mt19937_64 rng(static_cast<std::uint64_t>(factor_seed));
    std::uniform_real_distribution<double> f(0.5, 3.0);
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            b.add_edge(u, g.edge_target(e), g.edge_weight(e) * f(rng));
        }
    }
    return b.build();
}

}  // namespace

TEST("cch", "matches Dijkstra on a grid") {
    const auto g = testing::unit_grid(16, 16);
    CustomizableCH cch;
    cch.build(g);
    cch.customize();
    CHECK(cch.ready());
    expect_matches(g, cch, 200, 11, "grid");
}

TEST("cch", "matches Dijkstra on random and geographic graphs") {
    for (auto space : {CoordSpace::Planar, CoordSpace::Geographic}) {
        const auto g = testing::random_graph(160, 300, 77, space);
        CustomizableCH cch;
        cch.build(g);
        cch.customize();
        expect_matches(g, cch, 150, 12, "random");
    }
}

TEST("cch", "one build serves many metrics") {
    // The whole point: the shortcut *shape* is metric-independent, so a new
    // weight function needs customization only, never another contraction.
    const auto base = testing::random_graph(140, 260, 31);
    CustomizableCH cch;
    cch.build(base);

    for (int round = 0; round < 3; ++round) {
        const auto metric = reweight(base, 1000 + round);
        std::vector<double> w(static_cast<std::size_t>(metric.num_edges()));
        for (EdgeId e = 0; e < metric.num_edges(); ++e) {
            w[static_cast<std::size_t>(e)] = metric.edge_weight(e);
        }
        cch.customize(w);
        expect_matches(metric, cch, 80, 200 + round, "re-customized");
    }
}

TEST("cch", "customizing never rebuilds") {
    // The value of CCH is that a new metric costs a customization, not another
    // contraction -- but that is a *structural* property, and asserting it as a
    // timing comparison was wrong. On a 300-node graph build and customize are
    // naturally within noise of each other (MSVC measured 5.77 ms against
    // 5.59 ms and failed the build), and the asymmetry only appears at scale:
    // Jaipur is 31 ms build against 10 ms per metric, India's highway network
    // 101 ms against 35 ms. Those belong in a benchmark, not a unit test.
    //
    // What is testable here is that repeated customization leaves the built
    // structure alone and keeps answering correctly.
    const auto g = testing::random_graph(200, 380, 55);
    CustomizableCH cch;
    cch.build(g);
    const double build_ms = cch.stats().build_ms;
    const auto chordal = cch.stats().chordal_edges;
    CHECK(build_ms > 0.0);
    CHECK(chordal > 0);

    for (int round = 0; round < 4; ++round) {
        cch.customize();
        CHECK(cch.ready());
        // Build statistics must be untouched: a rebuild would change them.
        CHECK_MSG(cch.stats().build_ms == build_ms, "build ran again during customization");
        CHECK_EQ(cch.stats().chordal_edges, chordal);
        CHECK(cch.stats().customize_ms >= 0.0);
    }
    expect_matches(g, cch, 60, 404, "after repeated customization");
}

TEST("cch", "handles a time-dependent metric") {
    // The motivating case: the same road network at different hours.
    const auto g = testing::unit_grid(14, 14);
    TimeDependentModel m(g);
    CustomizableCH cch;
    cch.build(g);

    double quiet = 0, peak = 0;
    for (int hour : {3, 18}) {
        std::vector<double> w(static_cast<std::size_t>(g.num_edges()));
        for (EdgeId e = 0; e < g.num_edges(); ++e) {
            w[static_cast<std::size_t>(e)] = m.travel_time(e, hour * 3600.0);
        }
        cch.customize(w);
        const auto r = cch.query(0, g.num_nodes() - 1, {});
        CHECK(r.success);
        (hour == 3 ? quiet : peak) = r.path_cost;
    }
    CHECK_MSG(peak > quiet, "peak " << peak << " should exceed quiet " << quiet);
}

TEST("cch", "expands far less of the graph than Dijkstra") {
    const auto g = testing::unit_grid(26, 26);
    CustomizableCH cch;
    cch.build(g);
    cch.customize();
    auto dij = Registry::instance().create("dijkstra");
    long cch_total = 0, dij_total = 0;
    std::mt19937_64 rng(88);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int i = 0; i < 30; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto a = cch.query(s, t, {});
        const auto b = dij->run(g, s, t, {});
        if (!b.success) continue;
        CHECK_NEAR(a.path_cost, b.path_cost, 1e-9);
        cch_total += a.nodes_expanded;
        dij_total += b.nodes_expanded;
    }
    CHECK_MSG(cch_total < dij_total, "CCH " << cch_total << " vs Dijkstra " << dij_total);
}

TEST("cch", "handles one-way arcs and disconnected parts") {
    GraphBuilder b;
    for (int i = 0; i < 14; ++i) b.add_node(i, i % 5, i / 5);
    for (int i = 0; i < 6; ++i) b.add_edge(i, i + 1, 1.0 + i);     // one-way chain
    for (int i = 8; i < 13; ++i) b.add_undirected(i, i + 1, 2.0);  // separate island
    const auto g = b.build();

    CustomizableCH cch;
    cch.build(g);
    cch.customize();
    CHECK(cch.query(0, 6, {}).success);
    CHECK(!cch.query(6, 0, {}).success);   // against the one-way direction
    CHECK(!cch.query(0, 10, {}).success);  // across the gap
    expect_matches(g, cch, 60, 13, "directed + disconnected");
}

TEST("cch", "degenerate inputs") {
    GraphBuilder eb;
    const auto empty = eb.build();
    CustomizableCH cch;
    cch.build(empty);
    cch.customize();
    CHECK(!cch.query(0, 0, {}).success);

    CustomizableCH unbuilt;
    CHECK(!unbuilt.ready());
    CHECK(!unbuilt.query(0, 1, {}).success);

    // Querying after build but before customize must refuse, not guess.
    const auto g = testing::unit_grid(6, 6);
    CustomizableCH partial;
    partial.build(g);
    CHECK(!partial.ready());
    CHECK(!partial.query(0, 35, {}).success);
}
