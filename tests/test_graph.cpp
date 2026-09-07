#include <algorithm>
#include <set>

#include "agss/geo.hpp"
#include "agss/graph_builder.hpp"
#include "agss/kdtree.hpp"
#include "microtest.hpp"
#include "support.hpp"

using namespace agss;

TEST("graph", "CSR preserves every edge and its weight") {
    GraphBuilder b;
    for (int i = 0; i < 5; ++i) b.add_node(i * 10, i, i);  // sparse external ids
    b.add_edge(0, 10, 1.5);
    b.add_edge(0, 20, 2.5);
    b.add_edge(40, 0, 3.5);
    const auto g = b.build();

    CHECK_EQ(g.num_nodes(), 5);
    CHECK_EQ(g.num_edges(), 3);
    const NodeId n0 = g.lookup(0);
    CHECK_EQ(g.degree(n0), 2);
    std::set<double> ws;
    for (double w : g.weights(n0)) ws.insert(w);
    CHECK(ws.count(1.5) && ws.count(2.5));
    CHECK_EQ(g.degree(g.lookup(30)), 0);  // isolated node still addressable
}

TEST("graph", "external ids round-trip through dense indices") {
    GraphBuilder b;
    for (std::int64_t id : {5, 900, 17, 3}) b.add_node(id, 0, 0);
    const auto g = b.build();
    for (std::int64_t id : {5, 900, 17, 3}) {
        const NodeId n = g.lookup(id);
        CHECK(n != kInvalidNode);
        CHECK_EQ(g.external_id(n), id);
    }
    CHECK_EQ(g.lookup(12345), kInvalidNode);
}

TEST("graph", "duplicate node declarations keep the first coordinates") {
    GraphBuilder b;
    b.add_node(1, 10, 20);
    b.add_node(1, 99, 99);
    const auto g = b.build();
    CHECK_EQ(g.num_nodes(), 1);
    CHECK_NEAR(g.x(0), 10, 1e-12);
}

TEST("graph", "non-finite and negative weights are refused") {
    GraphBuilder b;
    b.add_node(0, 0, 0);
    b.add_node(1, 1, 1);
    CHECK(!b.add_edge(0, 1, std::numeric_limits<double>::infinity()).ok());
    CHECK(!b.add_edge(0, 1, std::nan("")).ok());
    CHECK(!b.add_edge(0, 1, -1.0).ok());
    CHECK(b.add_edge(0, 1, -1.0, /*allow_negative=*/true).ok());
}

TEST("geo", "haversine matches known city distances within 1%") {
    // Delhi (28.6139, 77.2090) to Mumbai (19.0760, 72.8777) is ~1150 km.
    const double d = geo::haversine(28.6139, 77.2090, 19.0760, 72.8777);
    CHECK_MSG(d > 1'130'000 && d < 1'170'000, "got " << d << " m");
    // Bengaluru to Chennai, ~290 km.
    const double d2 = geo::haversine(12.9716, 77.5946, 13.0827, 80.2707);
    CHECK_MSG(d2 > 280'000 && d2 < 300'000, "got " << d2 << " m");
}

TEST("geo", "equirectangular tracks haversine over city scales") {
    for (double dlat : {0.01, 0.05, 0.1}) {
        const double h = geo::haversine(12.97, 77.59, 12.97 + dlat, 77.59 + dlat);
        const double e = geo::equirectangular(12.97, 77.59, 12.97 + dlat, 77.59 + dlat);
        CHECK_MSG(std::abs(h - e) / h < 0.005,
                  "diverged by " << (100 * std::abs(h - e) / h) << "%");
    }
}

TEST("geo", "euclidean on lat/lon would overestimate, which is why it is gone") {
    // A degree of longitude at Delhi's latitude is much shorter than a degree
    // of latitude; treating them as equal inflates the estimate, which is what
    // broke A* admissibility on geographic graphs.
    const double true_m = geo::haversine(28.6, 77.0, 28.6, 78.0);  // 1 deg east
    const double lat_m = geo::haversine(28.6, 77.0, 29.6, 77.0);   // 1 deg north
    CHECK(true_m < lat_m * 0.9);
}

TEST("geo", "bearings and turn angles are sane") {
    CHECK_NEAR(geo::bearing(0, 0, 1, 0), 0.0, 1e-6);    // due north
    CHECK_NEAR(geo::bearing(0, 0, 0, 1), 90.0, 1e-6);   // due east
    CHECK_NEAR(geo::turn_angle(350, 10), 20.0, 1e-9);   // wraps forward
    CHECK_NEAR(geo::turn_angle(10, 350), -20.0, 1e-9);  // wraps back
}

TEST("kdtree", "nearest matches brute force on random points") {
    const auto g = testing::random_graph(500, 200, 4242);
    KdTree tree(g);
    std::mt19937_64 rng(2024);
    std::uniform_real_distribution<double> coord(0.0, 100.0);
    for (int trial = 0; trial < 200; ++trial) {
        const double qx = coord(rng), qy = coord(rng);
        NodeId brute = kInvalidNode;
        double best = std::numeric_limits<double>::infinity();
        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            const double d = geo::euclidean(qx, qy, g.x(u), g.y(u));
            if (d < best) {
                best = d;
                brute = u;
            }
        }
        const NodeId got = tree.nearest(qx, qy);
        CHECK_NEAR(geo::euclidean(qx, qy, g.x(got), g.y(got)), best, 1e-9);
        (void)brute;
    }
}

TEST("kdtree", "nearest works on geographic coordinates") {
    const auto g = testing::random_graph(300, 100, 31337, CoordSpace::Geographic);
    KdTree tree(g);
    std::mt19937_64 rng(11);
    std::uniform_real_distribution<double> lon(77.0, 78.0), lat(12.8, 13.8);
    for (int trial = 0; trial < 100; ++trial) {
        const double qx = lon(rng), qy = lat(rng);
        double best = std::numeric_limits<double>::infinity();
        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            best = std::min(best, geo::equirectangular(qy, qx, g.lat(u), g.lon(u)));
        }
        const NodeId got = tree.nearest(qx, qy);
        CHECK_NEAR(geo::equirectangular(qy, qx, g.lat(got), g.lon(got)), best, 1e-9);
    }
}

TEST("kdtree", "radius query matches brute force") {
    const auto g = testing::random_graph(300, 100, 777);
    KdTree tree(g);
    const double r = 15.0;
    auto got = tree.within(50.0, 50.0, r);
    std::vector<NodeId> brute;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        if (geo::euclidean(50.0, 50.0, g.x(u), g.y(u)) <= r) brute.push_back(u);
    }
    std::sort(got.begin(), got.end());
    std::sort(brute.begin(), brute.end());
    CHECK_EQ(got.size(), brute.size());
    CHECK(got == brute);
}

TEST("kdtree", "empty graph yields no nearest node") {
    GraphBuilder b;
    const auto g = b.build();
    KdTree tree(g);
    CHECK(tree.empty());
    CHECK_EQ(tree.nearest(0, 0), kInvalidNode);
}

TEST("graph", "admissibility is measured over every edge") {
    GraphBuilder b;
    b.add_node(0, 0, 0);
    b.add_node(1, 3, 4);     // straight-line distance 5
    b.add_edge(0, 1, 10.0);  // twice as long as the crow flies: fine
    const auto ok = b.build();
    CHECK_NEAR(ok.heuristic_admissibility(), 2.0, 1e-9);
    CHECK_NEAR(ok.worst_heuristic_shortfall(), 0.0, 1e-12);
    CHECK(ok.heuristic_is_admissible());

    GraphBuilder c;
    c.add_node(0, 0, 0);
    c.add_node(1, 3, 4);
    c.add_edge(0, 1, 2.5);  // half the straight line: heuristic over-estimates
    const auto bad = c.build();
    CHECK_NEAR(bad.heuristic_admissibility(), 0.5, 1e-9);
    CHECK_NEAR(bad.worst_heuristic_shortfall(), 2.5, 1e-9);
    CHECK(!bad.heuristic_is_admissible());
}

TEST("graph", "millimetre rounding does not read as inadmissible") {
    // Real OSM data contains sub-metre service roads. A 0.68 m edge that loses
    // 4 mm to CSV rounding scores 0.995 on ratio, while a genuinely wrong
    // weight on a long arterial can score 0.999 -- so the decision uses
    // absolute shortfall, and a ratio test alone would flag the wrong one.
    GraphBuilder tiny;
    tiny.add_node(0, 0.0, 0.0);
    tiny.add_node(1, 0.68, 0.0);
    tiny.add_edge(0, 1, 0.676);  // 4 mm short of 0.68
    const auto t = tiny.build();
    CHECK(t.heuristic_admissibility() < 0.995);   // ratio looks alarming
    CHECK(t.worst_heuristic_shortfall() < 0.01);  // absolute error is 4 mm
    CHECK_MSG(t.heuristic_is_admissible(), "millimetre rounding must not fail the check");

    GraphBuilder arterial;
    arterial.add_node(0, 0.0, 0.0);
    arterial.add_node(1, 1000.0, 0.0);
    arterial.add_edge(0, 1, 999.0);  // 1 m short over a kilometre
    const auto a = arterial.build();
    CHECK(a.heuristic_admissibility() > 0.998);  // ratio looks harmless
    CHECK_MSG(!a.heuristic_is_admissible(), "a metre of shortfall is a real modelling error");
}
