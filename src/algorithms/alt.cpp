#include "agss/alt.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <queue>

#include "agss/analysis.hpp"

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Entry {
    double key;
    NodeId node;
    bool operator>(const Entry& o) const { return key > o.key; }
};
using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>;

/// Single-source Dijkstra over the forward or reverse graph, filling `dist`.
void sweep(const Graph& g, NodeId src, std::vector<double>& dist,
           const std::vector<std::vector<std::pair<NodeId, double>>>* reverse) {
    std::fill(dist.begin(), dist.end(), kInf);
    std::vector<char> settled(dist.size(), 0);
    MinHeap pq;
    dist[src] = 0.0;
    pq.push({0.0, src});
    while (!pq.empty()) {
        const NodeId u = pq.top().node;
        pq.pop();
        if (settled[u]) continue;
        settled[u] = 1;
        if (reverse) {
            for (const auto& [v, w] : (*reverse)[u]) {
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    pq.push({dist[v], v});
                }
            }
        } else {
            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                const NodeId v = g.edge_target(e);
                if (dist[u] + g.edge_weight(e) < dist[v]) {
                    dist[v] = dist[u] + g.edge_weight(e);
                    pq.push({dist[v], v});
                }
            }
        }
    }
}

std::vector<std::vector<std::pair<NodeId, double>>> build_reverse(const Graph& g) {
    std::vector<std::vector<std::pair<NodeId, double>>> rev(
        static_cast<std::size_t>(g.num_nodes()));
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            rev[g.edge_target(e)].emplace_back(u, g.edge_weight(e));
        }
    }
    return rev;
}

}  // namespace

void AltIndex::build(const Graph& g, const BuildOptions& opts) {
    const auto t0 = Clock::now();
    graph_ = &g;
    built_ = false;
    stats_ = Stats{};
    landmarks_.clear();
    from_.clear();
    to_.clear();

    const auto n = static_cast<std::size_t>(g.num_nodes());
    if (n == 0) {
        built_ = true;
        return;
    }

    const auto rev = build_reverse(g);
    const int want = std::max(1, std::min<int>(opts.landmark_count, static_cast<int>(n)));

    // Landmarks are chosen from the largest strongly connected component, and
    // only nodes in it are candidates.
    //
    // This is not a refinement, it is required. A real extract has hundreds of
    // nodes stranded at the clipping boundary; seeding from one of those --
    // node 0 in the Delhi extract is exactly such a node -- meant the first
    // landmark reached almost nothing, every distance came back infinite, and
    // farthest-point selection collapsed to picking node 0 over and over. The
    // resulting bound was zero everywhere, so ALT degenerated silently into
    // plain Dijkstra: same 4,044 expansions, no error anywhere.
    //
    // Inside one strongly connected component every pair is mutually
    // reachable, so both sweeps are finite and every landmark says something
    // useful about every other node in it.
    std::vector<NodeId> candidates;
    {
        const auto scc = analysis::strongly_connected_components(g);
        std::size_t biggest = 0;
        for (std::size_t i = 0; i < scc.sizes.size(); ++i) {
            if (scc.sizes[i] > scc.sizes[biggest]) biggest = i;
        }
        for (NodeId v = 0; v < g.num_nodes(); ++v) {
            if (scc.component_of[v] == static_cast<NodeId>(biggest)) candidates.push_back(v);
        }
    }
    if (candidates.empty()) {
        built_ = true;
        stats_.build_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return;
    }

    std::vector<double> min_dist(n, kInf);
    std::vector<char> chosen(n, 0);

    NodeId next = candidates[static_cast<std::size_t>(opts.seed_node) % candidates.size()];

    for (int i = 0; i < want; ++i) {
        if (chosen[next]) break;  // nothing further to gain
        chosen[next] = 1;
        landmarks_.push_back(next);

        from_.emplace_back(n, kInf);
        sweep(g, next, from_.back(), nullptr);  // landmark -> everyone
        to_.emplace_back(n, kInf);
        sweep(g, next, to_.back(), &rev);  // everyone -> landmark

        for (NodeId v : candidates) {
            const double d = from_.back()[v];
            if (d < min_dist[v]) min_dist[v] = d;
        }

        // Furthest node from every landmark chosen so far. Landmarks want to
        // sit at the extremes: one in the middle gives a bound near zero for
        // routes passing either side, because the two triangle terms cancel.
        double best = -1.0;
        NodeId best_node = kInvalidNode;
        for (NodeId v : candidates) {
            if (chosen[v]) continue;
            const double d = min_dist[v];
            if (d != kInf && d > best) {
                best = d;
                best_node = v;
            }
        }
        if (best_node == kInvalidNode) break;
        next = best_node;
    }

    stats_.landmarks = static_cast<int>(landmarks_.size());
    stats_.bytes = static_cast<std::int64_t>(landmarks_.size() * n * 2 * sizeof(double));
    stats_.build_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    built_ = true;
}

double AltIndex::lower_bound(NodeId v, NodeId target) const {
    double best = 0.0;
    for (std::size_t i = 0; i < landmarks_.size(); ++i) {
        const auto& to_l = to_[i];      // d(x, L)
        const auto& from_l = from_[i];  // d(L, x)

        // Both differences are valid lower bounds; an infinite term means the
        // landmark tells us nothing about this pair, not that the bound is
        // infinite, so those are skipped rather than propagated.
        if (to_l[v] != kInf && to_l[target] != kInf) {
            best = std::max(best, to_l[v] - to_l[target]);
        }
        if (from_l[target] != kInf && from_l[v] != kInf) {
            best = std::max(best, from_l[target] - from_l[v]);
        }
    }
    return best;
}

SearchResult AltIndex::query(NodeId source, NodeId target, const SearchOptions& opts) const {
    SearchResult res;
    res.algorithm = "ALT (A* with landmarks)";
    res.time_complexity = "O((V + E) log V)";
    res.space_complexity = "O(k * V)";
    if (!built_ || graph_ == nullptr) return res;

    const Graph& g = *graph_;
    const auto n = g.num_nodes();
    if (source < 0 || target < 0 || source >= n || target >= n) return res;

    std::vector<double> dist(static_cast<std::size_t>(n), kInf);
    std::vector<NodeId> parent(static_cast<std::size_t>(n), kInvalidNode);
    std::vector<char> settled(static_cast<std::size_t>(n), 0);
    MinHeap pq;

    const auto t0 = Clock::now();
    dist[source] = 0.0;
    pq.push({lower_bound(source, target), source});
    if (opts.trace) opts.trace->discover(source, kInvalidNode);

    while (!pq.empty()) {
        const NodeId u = pq.top().node;
        pq.pop();
        if (settled[u]) continue;
        settled[u] = 1;
        ++res.nodes_expanded;
        if (opts.trace) opts.trace->expand(u);
        if (u == target) {
            res.success = true;
            break;
        }
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (!opts.edge_open(e)) continue;
            const NodeId v = g.edge_target(e);
            ++res.edges_relaxed;
            if (settled[v]) continue;
            const double nd = dist[u] + g.edge_weight(e);
            if (nd < dist[v]) {
                const bool first = dist[v] == kInf;
                dist[v] = nd;
                parent[v] = u;
                pq.push({nd + lower_bound(v, target), v});
                if (opts.trace) first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
            }
        }
    }
    res.algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (res.success) {
        res.path = reconstruct_path(parent, source, target);
        res.path_cost = dist[target];
    }
    return res;
}

/// Registry adapter, prepared once per graph like the CH one.
class AltAlgorithm : public Algorithm {
public:
    std::string name() const override { return "ALT (A* with landmarks)"; }
    std::string time_complexity() const override { return "O((V + E) log V)"; }
    std::string space_complexity() const override { return "O(k * V)"; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        if (prepared_for_ != &g) {
            index_.build(g);
            prepared_for_ = &g;
        }
        auto res = index_.query(source, target, opts);
        res.algorithm = name();
        return res;
    }

private:
    mutable AltIndex index_;
    mutable const Graph* prepared_for_ = nullptr;
};

void register_alt(Registry& r) {
    r.add("alt", [] { return std::unique_ptr<Algorithm>(new AltAlgorithm()); });
}

}  // namespace agss
