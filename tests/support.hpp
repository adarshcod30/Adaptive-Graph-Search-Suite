#pragma once

#include <random>
#include <string>
#include <vector>

#include "agss/graph_builder.hpp"

namespace testing {

/// Random connected weighted graph. A spanning tree first guarantees
/// connectivity, then extra edges add cycles so alternate routes exist.
inline agss::Graph random_graph(int n, int extra_edges, std::uint64_t seed,
                                agss::CoordSpace space = agss::CoordSpace::Planar) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> coord(0.0, 100.0);
    std::uniform_real_distribution<double> jitter(1.0, 3.0);

    agss::GraphBuilder b(space);
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i < n; ++i) {
        double x = coord(rng), y = coord(rng);
        if (space == agss::CoordSpace::Geographic) {
            x = 77.0 + x / 100.0;  // a degree-wide box near Bengaluru
            y = 12.8 + y / 100.0;
        }
        pts.emplace_back(x, y);
        b.add_node(i, x, y);
    }
    auto dist = [&](int u, int v) {
        if (space == agss::CoordSpace::Geographic) {
            return agss::geo::equirectangular(pts[u].second, pts[u].first, pts[v].second,
                                              pts[v].first);
        }
        return agss::geo::euclidean(pts[u].first, pts[u].second, pts[v].first, pts[v].second);
    };

    for (int i = 1; i < n; ++i) {
        std::uniform_int_distribution<int> pick(0, i - 1);
        const int p = pick(rng);
        b.add_undirected(i, p, dist(i, p) * jitter(rng));
    }
    std::uniform_int_distribution<int> any(0, n - 1);
    for (int e = 0; e < extra_edges; ++e) {
        const int u = any(rng), v = any(rng);
        if (u != v) b.add_undirected(u, v, dist(u, v) * jitter(rng));
    }
    return b.build();
}

/// Deterministic 4-connected grid with unit weights.
inline agss::Graph unit_grid(int w, int h) {
    agss::GraphBuilder b;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) b.add_node(r * w + c, c, h - 1 - r);
    }
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            const int u = r * w + c;
            if (c + 1 < w) b.add_undirected(u, u + 1, 1.0);
            if (r + 1 < h) b.add_undirected(u, u + w, 1.0);
        }
    }
    return b.build();
}

}  // namespace testing
