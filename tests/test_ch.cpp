// Contraction Hierarchies must agree with Dijkstra on every query. It is a
// preprocessing technique, not an approximation: a missed witness may cost an
// unnecessary shortcut, but never a different answer.
#include <random>

#include "agss/contraction_hierarchy.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

namespace {

/// Every CH answer must match Dijkstra in cost, and the returned path must be
/// a real edge sequence in the *original* graph -- which is what proves the
/// shortcuts were unpacked correctly.
void expect_matches_dijkstra(const Graph& g, const ContractionHierarchy& ch, int trials,
                             std::uint64_t seed, const char* label) {
    auto dij = Registry::instance().create("dijkstra");
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    int compared = 0;
    for (int i = 0; i < trials; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto want = dij->run(g, s, t, {});
        const auto got = ch.query(s, t, {});

        CHECK_MSG(got.success == want.success,
                  label << ": reachability disagrees for " << s << "->" << t);
        if (!want.success) continue;

        CHECK_MSG(mt::close(got.path_cost, want.path_cost, 1e-9),
                  label << ": " << s << "->" << t << " CH=" << got.path_cost
                        << " Dijkstra=" << want.path_cost);
        CHECK_MSG(got.path.front() == s && got.path.back() == t,
                  label << ": unpacked path has the wrong endpoints");
        const double real = path_cost(g, got.path);
        CHECK_MSG(real >= 0.0, label << ": unpacked path contains a non-edge");
        CHECK_MSG(mt::close(real, want.path_cost, 1e-9),
                  label << ": unpacked path costs " << real << ", expected " << want.path_cost);
        ++compared;
    }
    CHECK_MSG(compared > trials / 3, label << ": too few reachable pairs to be meaningful");
}

}  // namespace

TEST("ch", "matches Dijkstra on a grid") {
    const auto g = testing::unit_grid(24, 24);
    ContractionHierarchy ch;
    ch.build(g);
    CHECK(ch.ready());
    expect_matches_dijkstra(g, ch, 200, 11, "grid");
}

TEST("ch", "matches Dijkstra on a random weighted graph") {
    const auto g = testing::random_graph(400, 900, 1234);
    ContractionHierarchy ch;
    ch.build(g);
    expect_matches_dijkstra(g, ch, 250, 22, "random");
}

TEST("ch", "matches Dijkstra on a geographic graph") {
    const auto g = testing::random_graph(350, 800, 4321, CoordSpace::Geographic);
    ContractionHierarchy ch;
    ch.build(g);
    expect_matches_dijkstra(g, ch, 200, 33, "geographic");
}

TEST("ch", "handles disconnected components") {
    // Two islands with no arc between them: every cross query must fail, and
    // fail the same way Dijkstra does.
    GraphBuilder b;
    for (int i = 0; i < 12; ++i) b.add_node(i, i % 4, i / 4);
    for (int i = 0; i < 5; ++i) b.add_undirected(i, i + 1, 1.0 + i);
    for (int i = 6; i < 11; ++i) b.add_undirected(i, i + 1, 1.0 + i);
    const auto g = b.build();

    ContractionHierarchy ch;
    ch.build(g);
    CHECK(ch.query(0, 5, {}).success);
    CHECK(ch.query(6, 11, {}).success);
    CHECK(!ch.query(0, 7, {}).success);
    CHECK(!ch.query(11, 2, {}).success);
    expect_matches_dijkstra(g, ch, 60, 44, "disconnected");
}

TEST("ch", "handles one-way arcs") {
    // A directed cycle: going with the arcs is cheap, against them impossible.
    GraphBuilder b;
    for (int i = 0; i < 8; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 7; ++i) b.add_edge(i, i + 1, 1.0);
    const auto g = b.build();
    ContractionHierarchy ch;
    ch.build(g);

    const auto fwd = ch.query(0, 7, {});
    CHECK(fwd.success);
    CHECK_NEAR(fwd.path_cost, 7.0, 1e-9);
    CHECK_EQ(fwd.path.size(), std::size_t{8});
    CHECK(!ch.query(7, 0, {}).success);
}

TEST("ch", "trivial and degenerate inputs") {
    ContractionHierarchy empty;
    GraphBuilder eb;
    const auto eg = eb.build();
    empty.build(eg);
    CHECK(empty.ready());
    CHECK(!empty.query(0, 0, {}).success);

    GraphBuilder sb;
    sb.add_node(0, 0, 0);
    const auto single = sb.build();
    ContractionHierarchy ch;
    ch.build(single);
    const auto self = ch.query(0, 0, {});
    CHECK(self.success);
    CHECK_EQ(self.path.size(), std::size_t{1});

    // Querying before build() must not crash.
    ContractionHierarchy unbuilt;
    CHECK(!unbuilt.ready());
    CHECK(!unbuilt.query(0, 1, {}).success);
}

TEST("ch", "parallel and self edges do not multiply through contraction") {
    GraphBuilder b;
    for (int i = 0; i < 6; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 5; ++i) {
        b.add_undirected(i, i + 1, 5.0);
        b.add_undirected(i, i + 1, 2.0);  // cheaper duplicate
        b.add_undirected(i, i + 1, 9.0);
    }
    b.add_edge(2, 2, 1.0);  // self loop
    const auto g = b.build();
    ContractionHierarchy ch;
    ch.build(g);
    const auto r = ch.query(0, 5, {});
    CHECK(r.success);
    CHECK_NEAR(r.path_cost, 10.0, 1e-9);  // the 2.0 arcs win
    CHECK(path_cost(g, r.path) >= 0.0);
}

TEST("ch", "expands far less of the graph than Dijkstra") {
    // The whole point: preprocessing buys a much smaller search.
    const auto g = testing::unit_grid(45, 45);
    ContractionHierarchy ch;
    ch.build(g);
    auto dij = Registry::instance().create("dijkstra");

    std::mt19937_64 rng(99);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    long ch_total = 0, dij_total = 0;
    for (int i = 0; i < 40; ++i) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto a = ch.query(s, t, {});
        const auto b = dij->run(g, s, t, {});
        if (!b.success) continue;
        CHECK_NEAR(a.path_cost, b.path_cost, 1e-9);
        ch_total += a.nodes_expanded;
        dij_total += b.nodes_expanded;
    }
    CHECK_MSG(ch_total < dij_total, "CH expanded " << ch_total << " vs Dijkstra " << dij_total);
}

TEST("ch", "preprocessing reports sane statistics") {
    const auto g = testing::random_graph(300, 700, 777);
    ContractionHierarchy ch;
    ch.build(g);
    const auto& st = ch.stats();
    CHECK_EQ(st.original_edges, g.num_edges());
    CHECK(st.shortcuts >= 0);
    CHECK(st.witness_searches > 0);
    CHECK(st.build_ms >= 0.0);
    CHECK(st.edge_growth >= 1.0);
    // Every node gets a distinct rank.
    std::vector<char> seen(static_cast<std::size_t>(g.num_nodes()), 0);
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        const auto l = ch.level(v);
        CHECK(l >= 0 && l < g.num_nodes());
        CHECK_MSG(!seen[l], "two nodes share contraction rank " << l);
        seen[l] = 1;
    }
}

TEST("ch", "tighter witness bounds add shortcuts but never change answers") {
    const auto g = testing::random_graph(250, 550, 8888);
    ContractionHierarchy weak, strong;
    ContractionHierarchy::BuildOptions tight;
    tight.witness_hop_limit = 1;
    tight.witness_node_limit = 3;
    weak.build(g, tight);
    strong.build(g);

    // A weaker witness search cannot find as many alternatives, so it must
    // insert at least as many shortcuts -- and still answer identically.
    CHECK_MSG(weak.stats().shortcuts >= strong.stats().shortcuts,
              "weak=" << weak.stats().shortcuts << " strong=" << strong.stats().shortcuts);
    expect_matches_dijkstra(g, weak, 120, 55, "weak witness");
}

TEST("ch", "stays compact on road-like graphs") {
    // Edge growth is only meaningful on graphs with the structure CH exploits.
    // A grid stands in for a road network here; on a dense random graph the
    // shortcuts genuinely compound (measured above 9x), which is a property of
    // the input, not a defect in the implementation.
    const auto grid = testing::unit_grid(40, 40);
    ContractionHierarchy ch;
    ch.build(grid);
    CHECK_MSG(ch.stats().edge_growth < 3.0, "grid edge growth " << ch.stats().edge_growth);
    CHECK(!ch.stats().aborted);
}

TEST("ch", "an exhausted preprocessing budget still answers correctly") {
    // Bailing out must degrade effectiveness, never correctness: uncontracted
    // nodes keep a level above every contracted one, so the upward search
    // still reaches them.
    const auto g = testing::random_graph(300, 700, 246);
    ContractionHierarchy::BuildOptions tiny;
    tiny.budget_ms = 1.0;
    ContractionHierarchy ch;
    ch.build(g, tiny);
    CHECK(ch.ready());
    CHECK_MSG(ch.stats().aborted, "a 1 ms budget should not finish this graph");

    // Levels must still be a permutation, or the upward search breaks.
    std::vector<char> seen(static_cast<std::size_t>(g.num_nodes()), 0);
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        const auto l = ch.level(v);
        CHECK(l >= 0 && l < g.num_nodes());
        CHECK_MSG(!seen[l], "duplicate level after abort");
        seen[l] = 1;
    }
    expect_matches_dijkstra(g, ch, 120, 66, "aborted build");
}
