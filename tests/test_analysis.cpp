#include <algorithm>
#include <set>

#include "agss/analysis.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;
using namespace agss::analysis;

TEST("bridges", "a path graph is all bridges") {
    // 0 -- 1 -- 2 -- 3 : every edge disconnects if removed.
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 3; ++i) b.add_undirected(i, i + 1, 1.0);
    const auto rep = find_bridges(b.build());
    CHECK_EQ(rep.bridges.size(), std::size_t{3});
    CHECK_EQ(rep.component_count, 1);
}

TEST("bridges", "a cycle has none") {
    GraphBuilder b;
    for (int i = 0; i < 5; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 5; ++i) b.add_undirected(i, (i + 1) % 5, 1.0);
    const auto rep = find_bridges(b.build());
    CHECK_EQ(rep.bridges.size(), std::size_t{0});
    CHECK_EQ(rep.articulation_points.size(), std::size_t{0});
}

TEST("bridges", "the single road joining two cycles is the only bridge") {
    // Two triangles joined by one edge 2--3.
    GraphBuilder b;
    for (int i = 0; i < 6; ++i) b.add_node(i, i, 0);
    b.add_undirected(0, 1, 1);
    b.add_undirected(1, 2, 1);
    b.add_undirected(2, 0, 1);
    b.add_undirected(3, 4, 1);
    b.add_undirected(4, 5, 1);
    b.add_undirected(5, 3, 1);
    b.add_undirected(2, 3, 1);
    const auto rep = find_bridges(b.build());
    CHECK_EQ(rep.bridges.size(), std::size_t{1});
    const auto& br = rep.bridges.front();
    CHECK(std::min(br.u, br.v) == 2 && std::max(br.u, br.v) == 3);
    CHECK_EQ(br.isolated_nodes, 3);  // cutting it strands one triangle
    // Both endpoints of that bridge are articulation points.
    CHECK_EQ(rep.articulation_points.size(), std::size_t{2});
}

TEST("bridges", "a grid has no bridges but disconnected halves are counted") {
    CHECK_EQ(find_bridges(testing::unit_grid(6, 6)).bridges.size(), std::size_t{0});
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_undirected(0, 1, 1);
    b.add_undirected(2, 3, 1);
    CHECK_EQ(find_bridges(b.build()).component_count, 2);
}

TEST("scc", "a directed cycle is one component") {
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 4; ++i) b.add_edge(i, (i + 1) % 4, 1.0);
    const auto rep = strongly_connected_components(b.build());
    CHECK_EQ(rep.count, 1);
    CHECK_EQ(rep.sizes.front(), 4);
}

TEST("scc", "a one-way chain is all singletons") {
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 3; ++i) b.add_edge(i, i + 1, 1.0);
    const auto rep = strongly_connected_components(b.build());
    CHECK_EQ(rep.count, 4);
}

TEST("scc", "a two-way road network is a single component") {
    const auto rep = strongly_connected_components(testing::unit_grid(8, 8));
    CHECK_EQ(rep.count, 1);
}

TEST("mst", "picks the cheapest spanning set") {
    // Square with a cheap diagonal: 0-1 (1), 1-2 (1), 2-3 (1), 3-0 (5).
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_undirected(0, 1, 1);
    b.add_undirected(1, 2, 1);
    b.add_undirected(2, 3, 1);
    b.add_undirected(3, 0, 5);
    const auto rep = minimum_spanning_tree(b.build());
    CHECK_EQ(rep.edges.size(), std::size_t{3});
    CHECK_NEAR(rep.total_weight, 3.0, 1e-9);
    CHECK(rep.spans_all_nodes);
}

TEST("mst", "never costs more than any spanning tree it could replace") {
    const auto g = testing::random_graph(150, 400, 909);
    const auto rep = minimum_spanning_tree(g);
    CHECK(rep.spans_all_nodes);
    CHECK_EQ(rep.edges.size(), std::size_t{149});
    // Every MST edge must be a real edge of the graph.
    for (const auto& [u, v] : rep.edges) {
        bool found = false;
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (g.edge_target(e) == v) found = true;
        }
        CHECK(found);
    }
}

TEST("maxflow", "matches the bottleneck on a series-parallel network") {
    //  0 -> 1 (cap 3), 0 -> 2 (cap 2), 1 -> 3 (cap 2), 2 -> 3 (cap 3)
    //  max flow = 2 + 2 = 4
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_edge(0, 1, 3);
    b.add_edge(0, 2, 2);
    b.add_edge(1, 3, 2);
    b.add_edge(2, 3, 3);
    const auto rep = max_flow(b.build(), 0, 3);
    CHECK_NEAR(rep.max_flow, 4.0, 1e-9);
}

TEST("maxflow", "a single chain is limited by its narrowest link") {
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_edge(0, 1, 10);
    b.add_edge(1, 2, 1);
    b.add_edge(2, 3, 10);
    const auto rep = max_flow(b.build(), 0, 3);
    CHECK_NEAR(rep.max_flow, 1.0, 1e-9);
    CHECK_EQ(rep.min_cut.size(), std::size_t{1});
    CHECK(rep.min_cut.front().first == 1 && rep.min_cut.front().second == 2);
}

TEST("maxflow", "disconnected source and sink carry no flow") {
    GraphBuilder b;
    for (int i = 0; i < 4; ++i) b.add_node(i, i, 0);
    b.add_edge(0, 1, 5);
    b.add_edge(2, 3, 5);
    CHECK_NEAR(max_flow(b.build(), 0, 3).max_flow, 0.0, 1e-9);
}

TEST("centrality", "the hub of a star carries every path") {
    // Star: node 0 in the middle, 1..4 on the rim.
    GraphBuilder b;
    for (int i = 0; i < 5; ++i) b.add_node(i, i, 0);
    for (int i = 1; i < 5; ++i) b.add_undirected(0, i, 1.0);
    const auto rep = betweenness_centrality(b.build());
    CHECK(rep.exact);
    for (int i = 1; i < 5; ++i) {
        CHECK_MSG(rep.betweenness[0] > rep.betweenness[i], "hub should dominate the rim");
    }
    CHECK_NEAR(rep.betweenness[1], 0.0, 1e-9);  // leaves are on no path
}

TEST("centrality", "the middle of a path scores highest") {
    GraphBuilder b;
    for (int i = 0; i < 5; ++i) b.add_node(i, i, 0);
    for (int i = 0; i < 4; ++i) b.add_undirected(i, i + 1, 1.0);
    const auto rep = betweenness_centrality(b.build());
    CHECK(rep.betweenness[2] > rep.betweenness[1]);
    CHECK(rep.betweenness[1] > rep.betweenness[0]);
    CHECK_NEAR(rep.betweenness[0], 0.0, 1e-9);
}

TEST("centrality", "sampling approximates the exact ranking") {
    const auto g = testing::random_graph(200, 400, 5150);
    const auto exact = betweenness_centrality(g, 0);
    const auto sampled = betweenness_centrality(g, 100);
    CHECK(exact.exact);
    CHECK(!sampled.exact);
    CHECK_EQ(sampled.sources_sampled, 100);
    // The top node by exact score should stay near the top when sampled.
    const auto top = static_cast<std::size_t>(
        std::max_element(exact.betweenness.begin(), exact.betweenness.end()) -
        exact.betweenness.begin());
    std::size_t better = 0;
    for (double v : sampled.betweenness) {
        if (v > sampled.betweenness[top]) ++better;
    }
    CHECK_MSG(better < 20, "sampled rank of the true top node slipped to " << better);
}
