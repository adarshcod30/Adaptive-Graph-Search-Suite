#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stack>
#include <vector>

#include "agss/analysis.hpp"

namespace agss::analysis {
namespace {
using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

CentralityReport betweenness_centrality(const Graph& g, std::int64_t sample_sources,
                                        std::uint64_t seed) {
    CentralityReport rep;
    const auto t0 = Clock::now();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    rep.betweenness.assign(n, 0.0);
    if (n == 0) {
        rep.elapsed_ms = ms_since(t0);
        return rep;
    }

    std::vector<NodeId> sources(n);
    std::iota(sources.begin(), sources.end(), 0);
    if (sample_sources > 0 && static_cast<std::size_t>(sample_sources) < n) {
        std::mt19937_64 rng(seed);
        std::shuffle(sources.begin(), sources.end(), rng);
        sources.resize(static_cast<std::size_t>(sample_sources));
        rep.exact = false;
    }
    rep.sources_sampled = static_cast<std::int64_t>(sources.size());

    // Brandes: one shortest-path DAG per source, then accumulate dependencies
    // back down it. This is what avoids the O(V^3) of computing all pairs and
    // then counting paths separately.
    std::vector<double> dist(n), delta(n), sigma(n);
    std::vector<std::vector<NodeId>> preds(n);
    std::vector<NodeId> order;
    order.reserve(n);

    struct Entry {
        double key;
        NodeId node;
        bool operator>(const Entry& o) const { return key > o.key; }
    };

    for (NodeId s : sources) {
        std::fill(dist.begin(), dist.end(), kInf);
        std::fill(sigma.begin(), sigma.end(), 0.0);
        std::fill(delta.begin(), delta.end(), 0.0);
        for (auto& p : preds) p.clear();
        order.clear();

        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;
        std::vector<char> settled(n, 0);
        dist[s] = 0.0;
        sigma[s] = 1.0;
        pq.push({0.0, s});

        while (!pq.empty()) {
            const NodeId u = pq.top().node;
            pq.pop();
            if (settled[u]) continue;
            settled[u] = 1;
            order.push_back(u);

            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                const NodeId v = g.edge_target(e);
                const double nd = dist[u] + g.edge_weight(e);
                if (nd < dist[v] - 1e-12) {
                    dist[v] = nd;
                    sigma[v] = sigma[u];
                    preds[v].assign(1, u);
                    pq.push({nd, v});
                } else if (std::abs(nd - dist[v]) <= 1e-12 && !settled[v]) {
                    // Equal-cost alternative: this node lies on another
                    // shortest path, so its path count adds rather than replaces.
                    sigma[v] += sigma[u];
                    preds[v].push_back(u);
                }
            }
        }

        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const NodeId w = *it;
            for (NodeId v : preds[w]) {
                if (sigma[w] > 0.0) delta[v] += (sigma[v] / sigma[w]) * (1.0 + delta[w]);
            }
            if (w != s) rep.betweenness[w] += delta[w];
        }
    }

    // Scale a sampled estimate back up to a full-graph figure.
    if (!rep.exact && rep.sources_sampled > 0) {
        const double scale = static_cast<double>(n) / static_cast<double>(rep.sources_sampled);
        for (auto& b : rep.betweenness) b *= scale;
    }
    rep.elapsed_ms = ms_since(t0);
    return rep;
}

}  // namespace agss::analysis
