// The project's real oracle: algorithms that all claim optimality must agree,
// on every graph, for every query. No golden files, no hand-computed answers.
#include <algorithm>
#include <random>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/kshortest.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

namespace {

std::vector<std::string> optimal_keys() {
    std::vector<std::string> out;
    for (const auto& k : Registry::instance().keys()) {
        auto a = Registry::instance().create(k);
        if (a->guarantees_optimal()) out.push_back(k);
    }
    return out;
}

}  // namespace

TEST("differential", "every optimal algorithm agrees on cost (planar, 200 nodes)") {
    const auto g = testing::random_graph(200, 400, 7);
    const auto keys = optimal_keys();
    CHECK(keys.size() >= 5);

    std::mt19937_64 rng(99);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int trial = 0; trial < 400; ++trial) {
        const NodeId s = pick(rng), t = pick(rng);
        double reference = -1.0;
        std::string ref_key;
        for (const auto& k : keys) {
            const auto res = Registry::instance().create(k)->run(g, s, t, {});
            if (!res.success) continue;
            const double real = path_cost(g, res.path);
            CHECK_MSG(real >= 0.0, k << " returned a path containing a non-edge");
            if (reference < 0.0) {
                reference = real;
                ref_key = k;
            } else {
                CHECK_MSG(mt::close(real, reference, 1e-9), k << "=" << real << " but " << ref_key
                                                              << "=" << reference << " for " << s
                                                              << "->" << t);
            }
        }
    }
}

TEST("differential", "optimal algorithms agree on geographic graphs") {
    const auto g = testing::random_graph(150, 300, 21, CoordSpace::Geographic);
    const auto keys = optimal_keys();
    std::mt19937_64 rng(5);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int trial = 0; trial < 250; ++trial) {
        const NodeId s = pick(rng), t = pick(rng);
        double reference = -1.0;
        for (const auto& k : keys) {
            const auto res = Registry::instance().create(k)->run(g, s, t, {});
            if (!res.success) continue;
            const double real = path_cost(g, res.path);
            if (reference < 0.0)
                reference = real;
            else
                CHECK_MSG(mt::close(real, reference, 1e-9), k << " disagreed on a geo graph");
        }
    }
}

TEST("differential", "every returned path is a real edge sequence") {
    const auto g = testing::random_graph(120, 260, 31);
    std::mt19937_64 rng(1234);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (const auto& k : Registry::instance().keys()) {
        auto alg = Registry::instance().create(k);
        for (int trial = 0; trial < 60; ++trial) {
            const NodeId s = pick(rng), t = pick(rng);
            const auto res = alg->run(g, s, t, {});
            if (!res.success) continue;
            CHECK_MSG(res.path.front() == s, k << " path does not start at the source");
            CHECK_MSG(res.path.back() == t, k << " path does not end at the target");
            CHECK_MSG(path_cost(g, res.path) >= 0.0, k << " path contains a non-edge");
        }
    }
}

TEST("differential", "BFS is hop-minimal on unit-weight grids") {
    const auto g = testing::unit_grid(12, 12);
    auto bfs = Registry::instance().create("bfs");
    auto dij = Registry::instance().create("dijkstra");
    std::mt19937_64 rng(77);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int trial = 0; trial < 150; ++trial) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto b = bfs->run(g, s, t, {});
        const auto d = dij->run(g, s, t, {});
        CHECK_EQ(b.success, d.success);
        if (!b.success) continue;
        // With unit weights, hop count and cost coincide, so BFS must match.
        CHECK_NEAR(b.path.size() - 1, d.path_cost, 1e-9);
        CHECK(b.path.size() <= d.path.size());
    }
}

TEST("differential", "heuristic search never beats the optimum") {
    const auto g = testing::random_graph(180, 360, 42);
    auto dij = Registry::instance().create("dijkstra");
    for (const auto& k : {"greedy", "bfs", "dfs"}) {
        auto alg = Registry::instance().create(k);
        std::mt19937_64 rng(8);
        std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
        for (int trial = 0; trial < 120; ++trial) {
            const NodeId s = pick(rng), t = pick(rng);
            const auto opt = dij->run(g, s, t, {});
            const auto sub = alg->run(g, s, t, {});
            CHECK_MSG(opt.success == sub.success,
                      std::string(k) << " disagrees with Dijkstra on reachability");
            if (!opt.success) continue;
            // Suboptimal algorithms may be worse, never better.
            CHECK_MSG(path_cost(g, sub.path) >= opt.path_cost - 1e-9,
                      std::string(k) << " reported a cheaper cost than the optimum");
        }
    }
}

TEST("differential", "A* stays optimal under the geographic heuristic") {
    // The old Euclidean-on-lat/lon heuristic over-estimated and silently broke
    // admissibility; this pins the corrected behaviour.
    const auto g = testing::random_graph(200, 500, 555, CoordSpace::Geographic);
    auto astar = Registry::instance().create("astar");
    auto dij = Registry::instance().create("dijkstra");
    std::mt19937_64 rng(313);
    std::uniform_int_distribution<NodeId> pick(0, g.num_nodes() - 1);
    for (int trial = 0; trial < 300; ++trial) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto a = astar->run(g, s, t, {});
        const auto d = dij->run(g, s, t, {});
        CHECK_EQ(a.success, d.success);
        if (a.success) CHECK_NEAR(a.path_cost, d.path_cost, 1e-9);
    }
}

TEST("differential", "bidirectional Dijkstra matches and expands fewer nodes") {
    const auto g = testing::unit_grid(30, 30);
    auto bi = Registry::instance().create("bidijkstra");
    auto dij = Registry::instance().create("dijkstra");
    long bi_total = 0, dij_total = 0;
    for (NodeId t : {449, 599, 899}) {
        const auto b = bi->run(g, 0, t, {});
        const auto d = dij->run(g, 0, t, {});
        CHECK(b.success && d.success);
        CHECK_NEAR(b.path_cost, d.path_cost, 1e-9);
        CHECK(path_cost(g, b.path) >= 0.0);
        bi_total += b.nodes_expanded;
        dij_total += d.nodes_expanded;
    }
    CHECK_MSG(bi_total < dij_total,
              "bidirectional expanded " << bi_total << " vs unidirectional " << dij_total);
}

TEST("differential", "Yen's routes are distinct and cost-ordered") {
    const auto g = testing::random_graph(120, 300, 606);
    const auto rep = k_shortest_paths(g, 0, 60, 5);
    CHECK(!rep.routes.empty());
    for (std::size_t i = 0; i < rep.routes.size(); ++i) {
        CHECK_MSG(path_cost(g, rep.routes[i].path) >= 0.0, "route " << i << " contains a non-edge");
        CHECK_NEAR(rep.routes[i].cost, path_cost(g, rep.routes[i].path), 1e-9);
        // Loopless: no node repeats within a route.
        auto sorted = rep.routes[i].path;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
        if (i > 0) {
            CHECK_MSG(rep.routes[i].cost >= rep.routes[i - 1].cost - 1e-9,
                      "routes are not in increasing cost order");
            CHECK(rep.routes[i].path != rep.routes[i - 1].path);
        }
    }
    // The first route must be the true shortest path.
    const auto d = Registry::instance().create("dijkstra")->run(g, 0, 60, {});
    if (d.success) CHECK_NEAR(rep.routes.front().cost, d.path_cost, 1e-9);
}

TEST("differential", "Dial declines fractional weights instead of rounding them") {
    // Bucket indices are distances, so a fractional weight can only be stored
    // by rounding -- which optimises the wrong cost function. Caught by the
    // verify harness at 100 samples after slipping past a 40-trial test.
    auto dial = Registry::instance().create("dial");
    auto dij = Registry::instance().create("dijkstra");

    const auto fractional = testing::random_graph(120, 240, 7);
    const auto declined = dial->run(fractional, 0, 60, {});
    CHECK_MSG(!declined.success, "Dial must not answer on fractional weights");
    CHECK(declined.algorithm.find("integer edge weights") != std::string::npos);

    // On integer weights it must be exact, over enough trials to mean something.
    const auto grid = testing::unit_grid(20, 20);
    std::mt19937_64 rng(4242);
    std::uniform_int_distribution<NodeId> pick(0, grid.num_nodes() - 1);
    int compared = 0;
    for (int trial = 0; trial < 300; ++trial) {
        const NodeId s = pick(rng), t = pick(rng);
        const auto a = dial->run(grid, s, t, {});
        const auto b = dij->run(grid, s, t, {});
        CHECK_EQ(a.success, b.success);
        if (!a.success) continue;
        CHECK_NEAR(a.path_cost, b.path_cost, 1e-9);
        CHECK(path_cost(grid, a.path) >= 0.0);
        ++compared;
    }
    CHECK(compared > 250);
}

TEST("differential", "an algorithm that declines says so and returns nothing") {
    // A declined run must be distinguishable from a failed search: no path, no
    // claimed success, and a name that explains itself.
    const auto big = testing::random_graph(6000, 8000, 11);
    const auto res = Registry::instance().create("floydwarshall")->run(big, 0, 100, {});
    CHECK(!res.success);
    CHECK(res.path.empty());
    CHECK(res.algorithm.find("skipped") != std::string::npos);
}

TEST("differential", "A* is only trusted where the heuristic is admissible") {
    // An edge shorter than the straight line between its endpoints turns the
    // heuristic into an over-estimate. The bundled generator used to emit
    // exactly that (0.9x), which made A* disagree with Dijkstra on ~2% of
    // queries while looking like an algorithm bug.
    GraphBuilder bad;
    bad.add_node(0, 0, 0);
    bad.add_node(1, 100, 0);
    bad.add_node(2, 50, 1);
    bad.add_edge(0, 1, 50.0);  // weight 50 across a straight-line gap of 100
    bad.add_edge(0, 2, 10.0);
    bad.add_edge(2, 1, 10.0);
    const auto g = bad.build();
    CHECK(!g.heuristic_is_admissible());
    CHECK(g.heuristic_admissibility() < 1.0);

    // Every bundled map must stay on the right side of that line.
    for (auto space : {CoordSpace::Planar, CoordSpace::Geographic}) {
        const auto gen = testing::random_graph(300, 600, 2468, space);
        CHECK_MSG(gen.heuristic_is_admissible(), "generated graph has an edge at "
                                                     << gen.heuristic_admissibility()
                                                     << "x its straight-line length");
    }
}
