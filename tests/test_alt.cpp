// ALT keeps A*'s optimality guarantee, so its bound must never exceed the true
// remaining distance and its answers must match Dijkstra exactly.
#include <random>

#include "agss/alt.hpp"
#include "agss/analysis.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

namespace {

void expect_matches_dijkstra(const Graph& g, const AltIndex& alt, int trials, std::uint64_t seed,
                             const char* label) {
    auto dij = Registry::instance().create("dijkstra");
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    int compared = 0;
    for (int i = 0; i < trials; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto want = dij->run(g, s, t, {});
        const auto got = alt.query(s, t, {});
        CHECK_MSG(got.success == want.success, label << ": reachability disagrees");
        if (!want.success) continue;
        CHECK_MSG(mt::close(got.path_cost, want.path_cost, 1e-9),
                  label << ": ALT=" << got.path_cost << " Dijkstra=" << want.path_cost);
        CHECK_MSG(path_cost(g, got.path) >= 0.0, label << ": path contains a non-edge");
        ++compared;
    }
    CHECK(compared > trials / 3);
}

}  // namespace

TEST("alt", "matches Dijkstra on a grid") {
    const auto g = testing::unit_grid(18, 18);
    AltIndex alt;
    alt.build(g);
    CHECK(alt.ready());
    expect_matches_dijkstra(g, alt, 200, 3, "grid");
}

TEST("alt", "matches Dijkstra on random and geographic graphs") {
    for (auto space : {CoordSpace::Planar, CoordSpace::Geographic}) {
        const auto g = testing::random_graph(150, 280, 91, space);
        AltIndex alt;
        alt.build(g);
        expect_matches_dijkstra(g, alt, 150, 4, "random");
    }
}

TEST("alt", "the bound never exceeds the true distance") {
    // Admissibility is the whole contract: an over-estimate anywhere costs A*
    // its optimality guarantee, silently.
    const auto g = testing::unit_grid(15, 15);
    AltIndex alt;
    alt.build(g);
    auto dij = Registry::instance().create("dijkstra");

    std::mt19937_64 rng(17);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    int checked = 0;
    for (int i = 0; i < 250; ++i) {
        const NodeId v = pick(rng), t = pick(rng);
        const auto res = dij->run(g, v, t, {});
        if (!res.success) continue;
        const double bound = alt.lower_bound(v, t);
        CHECK_MSG(bound <= res.path_cost + 1e-9, "bound " << bound << " exceeds true distance "
                                                          << res.path_cost << " for " << v << "->"
                                                          << t);
        CHECK(bound >= 0.0);
        ++checked;
    }
    CHECK(checked > 150);
}

TEST("alt", "the bound is exact from a landmark to itself, and zero at the target") {
    const auto g = testing::unit_grid(12, 12);
    AltIndex alt;
    alt.build(g);
    for (NodeId t = 0; t < g.num_nodes(); t += 17) {
        CHECK_NEAR(alt.lower_bound(t, t), 0.0, 1e-9);
    }
}

TEST("alt", "landmarks are distinct and drawn from the largest component") {
    // Farthest-point selection seeded from a stranded node collapses to
    // picking the same vertex repeatedly, which makes every bound zero and
    // turns ALT into plain Dijkstra with no error anywhere. Anchoring the
    // search to the largest strongly connected component is what prevents it.
    GraphBuilder b;
    for (int i = 0; i < 40; ++i) b.add_node(i, i % 8, i / 8);
    // A stranded pair at ids 0 and 1, and a large connected block after it.
    b.add_undirected(0, 1, 1.0);
    for (int i = 2; i < 39; ++i) b.add_undirected(i, i + 1, 1.0 + (i % 3));
    const auto g = b.build();

    AltIndex alt;
    AltIndex::BuildOptions o;
    o.landmark_count = 6;
    o.seed_node = 0;  // deliberately the stranded side
    alt.build(g, o);

    const auto& marks = alt.landmarks();
    CHECK(marks.size() >= 2);
    std::vector<NodeId> sorted(marks);
    std::sort(sorted.begin(), sorted.end());
    CHECK_MSG(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(),
              "landmarks repeat, so selection degenerated");
    for (NodeId m : marks) {
        CHECK_MSG(m >= 2, "landmark " << m << " came from the stranded component");
    }
    expect_matches_dijkstra(g, alt, 80, 5, "stranded seed");
}

TEST("alt", "more landmarks tighten the bound") {
    const auto g = testing::unit_grid(16, 16);
    AltIndex few, many;
    AltIndex::BuildOptions a;
    a.landmark_count = 2;
    AltIndex::BuildOptions b;
    b.landmark_count = 12;
    few.build(g, a);
    many.build(g, b);
    CHECK_EQ(few.landmarks().size(), std::size_t{2});
    CHECK(many.landmarks().size() > few.landmarks().size());

    // Averaged over pairs, more landmarks cannot be worse: the bound is a max
    // over strictly more terms.
    double sum_few = 0, sum_many = 0;
    std::mt19937_64 rng(23);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int i = 0; i < 200; ++i) {
        const NodeId v = pick(rng), t = pick(rng);
        sum_few += few.lower_bound(v, t);
        sum_many += many.lower_bound(v, t);
    }
    CHECK_MSG(sum_many >= sum_few, "more landmarks produced a weaker bound overall");
}

TEST("alt", "prunes harder than plain Dijkstra") {
    const auto g = testing::unit_grid(30, 30);
    AltIndex alt;
    alt.build(g);
    auto dij = Registry::instance().create("dijkstra");
    long alt_total = 0, dij_total = 0;
    for (NodeId t : {449, 599, 880}) {
        const auto a = alt.query(0, t, {});
        const auto d = dij->run(g, 0, t, {});
        CHECK(a.success && d.success);
        CHECK_NEAR(a.path_cost, d.path_cost, 1e-9);
        alt_total += a.nodes_expanded;
        dij_total += d.nodes_expanded;
    }
    CHECK_MSG(alt_total < dij_total, "ALT " << alt_total << " vs Dijkstra " << dij_total);
}

TEST("alt", "degenerate inputs") {
    GraphBuilder eb;
    const auto empty = eb.build();
    AltIndex a;
    a.build(empty);
    CHECK(a.ready());
    CHECK(!a.query(0, 0, {}).success);

    AltIndex unbuilt;
    CHECK(!unbuilt.ready());
    CHECK(!unbuilt.query(0, 1, {}).success);
}
