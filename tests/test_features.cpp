#include <algorithm>
#include <fstream>
#include <sstream>

#include "agss/directions.hpp"
#include "agss/isochrone.hpp"
#include "agss/json.hpp"
#include "agss/transit.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

TEST("trace", "delta stream is linear, not quadratic, in graph size") {
    // The old format wrote the whole frontier and explored set every frame,
    // which is what made a 19.6k-node trace 980 MB. Each node should now be
    // discovered and expanded at most once.
    for (int side : {10, 20, 40}) {
        const auto g = testing::unit_grid(side, side);
        Trace tr;
        SearchOptions o;
        o.trace = &tr;
        const auto res = Registry::instance().create("dijkstra")->run(g, 0, g.num_nodes() - 1, o);
        CHECK(res.success);
        // Discover + expand are bounded by V; relax by E.
        CHECK_MSG(tr.size() <= static_cast<std::size_t>(2 * g.num_nodes() + g.num_edges()),
                  "trace had " << tr.size() << " events for V=" << g.num_nodes()
                               << " E=" << g.num_edges());
        std::vector<int> expanded(static_cast<std::size_t>(g.num_nodes()), 0);
        for (const auto& e : tr.events()) {
            if (e.op == Op::Expand) ++expanded[e.node];
        }
        for (int c : expanded) CHECK(c <= 1);
    }
}

TEST("trace", "events replay into the frontier and explored sets") {
    const auto g = testing::unit_grid(8, 8);
    Trace tr;
    SearchOptions o;
    o.trace = &tr;
    const auto res = Registry::instance().create("dijkstra")->run(g, 0, 63, o);
    CHECK(res.success);

    // Replay exactly as the client does.
    std::vector<NodeId> parent(static_cast<std::size_t>(g.num_nodes()), kInvalidNode);
    std::vector<char> in_frontier(static_cast<std::size_t>(g.num_nodes()), 0);
    std::vector<char> explored(static_cast<std::size_t>(g.num_nodes()), 0);
    for (const auto& e : tr.events()) {
        switch (e.op) {
            case Op::Discover:
                in_frontier[e.node] = 1;
                parent[e.node] = e.parent;
                break;
            case Op::Relax:
                parent[e.node] = e.parent;
                break;
            case Op::Expand:
                in_frontier[e.node] = 0;
                explored[e.node] = 1;
                break;
        }
    }
    CHECK(explored[63]);
    // The replayed parent pointers must rebuild the very same path.
    const auto replayed = reconstruct_path(parent, 0, 63);
    CHECK(replayed == res.path);
}

TEST("trace", "no trace is recorded when none is requested") {
    const auto g = testing::unit_grid(10, 10);
    const auto res = Registry::instance().create("astar")->run(g, 0, 99, {});
    CHECK(res.success);
    CHECK(res.nodes_expanded > 0);  // it still counts work, just records nothing
}

TEST("json", "trace output is well-formed and escapes control characters") {
    const auto g = testing::unit_grid(5, 5);
    Trace tr;
    SearchOptions o;
    o.trace = &tr;
    const auto res = Registry::instance().create("bfs")->run(g, 0, 24, o);
    std::ostringstream os;
    json::TraceDocument doc;
    doc.graph = &g;
    doc.result = &res;
    doc.trace = &tr;
    json::write_trace(os, doc);
    const std::string s = os.str();
    CHECK(s.find("\"formatVersion\": 2") != std::string::npos);
    CHECK(s.find("\"events\"") != std::string::npos);
    CHECK(s.find("\"algorithmMs\"") != std::string::npos);
    // Balanced braces is a cheap structural sanity check.
    int depth = 0;
    for (char c : s) {
        if (c == '{') ++depth;
        if (c == '}') --depth;
    }
    CHECK_EQ(depth, 0);
    CHECK_EQ(json::escape("a\"b\\c\nd"), std::string("a\\\"b\\\\c\\nd"));
    CHECK_EQ(json::number(std::numeric_limits<double>::infinity()), std::string("null"));
}

TEST("closures", "shutting a bridge forces a detour or cuts the route") {
    // Two triangles joined by the single edge 2--3.
    GraphBuilder b;
    for (int i = 0; i < 6; ++i) b.add_node(i, i, 0);
    b.add_undirected(0, 1, 1);
    b.add_undirected(1, 2, 1);
    b.add_undirected(2, 0, 1);
    b.add_undirected(3, 4, 1);
    b.add_undirected(4, 5, 1);
    b.add_undirected(5, 3, 1);
    b.add_undirected(2, 3, 1);
    const auto g = b.build();

    auto dij = Registry::instance().create("dijkstra");
    CHECK(dij->run(g, 0, 4, {}).success);

    ClosureMask mask(g.num_edges());
    CHECK_EQ(mask.close_road(g, 2, 3), std::size_t{2});  // both directions
    SearchOptions o;
    o.closures = &mask;
    CHECK_MSG(!dij->run(g, 0, 4, o).success, "closing the only bridge should sever the route");
}

TEST("closures", "a detour is taken when one exists") {
    // 0-1-2 direct (cost 10 via 1), plus a longer way round through 3.
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_undirected(0, 1, 1);
    b.add_undirected(1, 2, 1);
    b.add_undirected(0, 3, 5);
    b.add_undirected(3, 2, 5);
    const auto g = b.build();
    auto dij = Registry::instance().create("dijkstra");

    const auto before = dij->run(g, 0, 2, {});
    CHECK_NEAR(before.path_cost, 2.0, 1e-9);

    ClosureMask mask(g.num_edges());
    mask.close_road(g, 0, 1);
    SearchOptions o;
    o.closures = &mask;
    const auto after = dij->run(g, 0, 2, o);
    CHECK(after.success);
    CHECK_NEAR(after.path_cost, 10.0, 1e-9);  // forced onto the long way round
    CHECK(path_cost(g, after.path) >= 0.0);
}

TEST("isochrone", "bands nest and respect their cutoffs") {
    const auto g = testing::unit_grid(15, 15);
    const auto rep = isochrone(g, 0, {2.0, 5.0, 10.0});
    CHECK_EQ(rep.bands.size(), std::size_t{3});
    for (std::size_t i = 0; i < rep.bands.size(); ++i) {
        for (NodeId u : rep.bands[i].nodes) {
            CHECK(rep.cost_to[static_cast<std::size_t>(u)] <= rep.bands[i].cutoff + 1e-9);
        }
        if (i > 0) {
            // Larger budgets must reach at least as much ground.
            CHECK(rep.bands[i].nodes.size() >= rep.bands[i - 1].nodes.size());
        }
    }
    // On a unit grid, everything within 2 hops of a corner is a known set.
    CHECK_EQ(rep.bands[0].nodes.size(), std::size_t{6});
}

TEST("isochrone", "matches Dijkstra distances exactly") {
    const auto g = testing::random_graph(150, 300, 4004);
    const auto rep = isochrone(g, 0, {1000.0});
    auto dij = Registry::instance().create("dijkstra");
    for (NodeId t = 1; t < 30; ++t) {
        const auto res = dij->run(g, 0, t, {});
        if (res.success && res.path_cost <= 1000.0) {
            CHECK_NEAR(rep.cost_to[static_cast<std::size_t>(t)], res.path_cost, 1e-9);
        }
    }
}

TEST("isochrone", "convex hull wraps every point it is given") {
    const auto hull = convex_hull({{0, 0}, {4, 0}, {4, 4}, {0, 4}, {2, 2}});
    CHECK_EQ(hull.size(), std::size_t{4});  // the interior point is dropped
    CHECK_EQ(convex_hull({{1, 1}}).size(), std::size_t{1});
    CHECK_EQ(convex_hull({}).size(), std::size_t{0});
}

TEST("directions", "a straight run collapses to one instruction") {
    GraphBuilder b;
    for (int i = 0; i < 5; ++i) b.add_node(i, i, 0);  // all on the x axis
    for (int i = 0; i < 4; ++i) b.add_undirected(i, i + 1, 1.0);
    const auto g = b.build();
    const auto d = build_directions(g, {0, 1, 2, 3, 4});
    // Depart + one leg + arrive; no per-intersection noise.
    CHECK_EQ(d.steps.size(), std::size_t{2});
    CHECK(d.steps.front().maneuver == Maneuver::Depart);
    CHECK(d.steps.back().maneuver == Maneuver::Arrive);
    CHECK_NEAR(d.total_distance, 4.0, 1e-9);
}

TEST("directions", "a right-angle bend produces a turn") {
    GraphBuilder b;
    b.add_node(0, 0, 0);
    b.add_node(1, 0, 1);  // north of 0
    b.add_node(2, 1, 1);  // east of 1
    b.add_undirected(0, 1, 1.0);
    b.add_undirected(1, 2, 1.0);
    const auto g = b.build();
    const auto d = build_directions(g, {0, 1, 2});
    CHECK(d.steps.size() >= 3);
    CHECK_MSG(d.steps[1].maneuver == Maneuver::Right,
              "expected a right turn, got " << to_string(d.steps[1].maneuver));
    CHECK(d.steps[1].turn > 0);  // positive = right
}

TEST("directions", "a left bend is reported as a left") {
    GraphBuilder b;
    b.add_node(0, 0, 0);
    b.add_node(1, 0, 1);
    b.add_node(2, -1, 1);
    b.add_undirected(0, 1, 1.0);
    b.add_undirected(1, 2, 1.0);
    const auto d = build_directions(b.build(), {0, 1, 2});
    CHECK(d.steps[1].maneuver == Maneuver::Left);
    CHECK(d.steps[1].turn < 0);
}

TEST("directions", "degenerate paths produce nothing rather than crashing") {
    const auto g = testing::unit_grid(4, 4);
    CHECK_EQ(build_directions(g, {}).steps.size(), std::size_t{0});
    CHECK_EQ(build_directions(g, {0}).steps.size(), std::size_t{0});
}

namespace {

/// Two stations on one line, plus a small road grid underneath them.
transit::Network toy_network() {
    transit::Network net;
    net.stations.push_back({1, "Alpha", "Toy Metro", "Toyville", 12.9700, 77.5900});
    net.stations.push_back({2, "Beta", "Toy Metro", "Toyville", 12.9800, 77.6000});
    net.stations.push_back({3, "Gamma", "Toy Metro", "Toyville", 12.9900, 77.6100});
    net.links.push_back({1, 2, "Line 1", 120.0});
    net.links.push_back({2, 3, "Line 1", 120.0});
    net.systems = {"Toy Metro"};
    return net;
}

}  // namespace

TEST("transit", "rail-only graph links adjacent stations both ways") {
    auto built = transit::rail_only(toy_network());
    CHECK(built.ok());
    const auto& mm = built.value();
    CHECK_EQ(mm.graph.num_nodes(), 3);
    CHECK_EQ(mm.graph.num_edges(), 4);  // two links, both directions
    const auto res =
        Registry::instance()
            .create("dijkstra")
            ->run(mm.graph, mm.layers.station_node.at(1), mm.layers.station_node.at(3), {});
    CHECK(res.success);
    CHECK_NEAR(res.path_cost, 240.0, 1e-9);
}

TEST("transit", "combining stitches the rail layer onto the road layer") {
    // Road grid spanning the same patch of Bengaluru as the toy stations.
    GraphBuilder b(CoordSpace::Geographic);
    int id = 0;
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            b.add_node(id, 77.59 + j * 0.005, 12.97 + i * 0.005);
            ids.push_back(id++);
        }
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            const int u = i * 5 + j;
            if (j + 1 < 5) b.add_undirected(u, u + 1, 550.0);
            if (i + 1 < 5) b.add_undirected(u, u + 5, 550.0);
        }
    }
    const auto road = b.build();

    transit::BuildOptions o;
    o.transfer_radius_m = 800.0;
    auto built = transit::combine(road, toy_network(), o);
    CHECK(built.ok());
    const auto& mm = built.value();
    CHECK_EQ(mm.layers.road_node_count, 25);
    CHECK_EQ(mm.layers.station_node_count, 3);

    // The whole thing must be one connected graph: starting on a road node,
    // a station has to be reachable, which is only true if stitching worked.
    auto dij = Registry::instance().create("dijkstra");
    const auto to_station = dij->run(mm.graph, 0, mm.layers.station_node.at(3), {});
    CHECK_MSG(to_station.success, "road layer is not connected to the rail layer");
    CHECK(path_cost(mm.graph, to_station.path) >= 0.0);
}

TEST("transit", "a planar road graph is refused for multi-modal") {
    const auto planar = testing::unit_grid(4, 4);
    auto built = transit::combine(planar, toy_network());
    CHECK(!built.ok());
    CHECK(built.error().message.find("geographic") != std::string::npos);
}

TEST("transit", "filter selects by city or system") {
    auto net = toy_network();
    net.stations.push_back({4, "Delta", "Other Metro", "Elsewhere", 20.0, 75.0});
    CHECK_EQ(transit::filter(net, "Toyville").stations.size(), std::size_t{3});
    CHECK_EQ(transit::filter(net, "Other Metro").stations.size(), std::size_t{1});
    CHECK_EQ(transit::filter(net, "nowhere").stations.size(), std::size_t{0});
}
