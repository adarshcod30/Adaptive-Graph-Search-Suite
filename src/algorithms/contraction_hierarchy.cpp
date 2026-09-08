#include "agss/contraction_hierarchy.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <queue>
#include <unordered_map>

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();

inline std::uint64_t arc_key(NodeId a, NodeId b) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
           static_cast<std::uint32_t>(b);
}

struct HeapItem {
    double key;
    NodeId node;
    bool operator>(const HeapItem& o) const { return key > o.key; }
};
using MinHeap = std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<>>;

}  // namespace

/// Working state for the contraction pass. Kept out of the class so the
/// prepared hierarchy carries only what a query needs.
struct Builder {
    struct WArc {
        NodeId to;
        double weight;
        NodeId via;
    };

    const Graph& g;
    const ContractionHierarchy::BuildOptions& opts;
    std::vector<std::vector<WArc>> out, in;
    std::vector<char> contracted;
    std::vector<std::int32_t> level;
    std::vector<std::int32_t> contracted_neighbours;
    std::unordered_map<std::uint64_t, NodeId> via;   // shortcut -> bypassed node
    std::unordered_map<std::uint64_t, double> best;  // cheapest arc per pair
    std::int64_t witness_searches = 0;
    std::int64_t shortcuts = 0;
    MinHeap pq_buf;

    // Reused across witness searches so the inner loop allocates nothing.
    // These are the hot buffers: a witness search runs for every neighbour
    // pair of every priority evaluation, so allocating here dominated
    // preprocessing -- 52 s on a 400-node graph before this was hoisted out.
    std::vector<double> dist;
    std::vector<int> hop;
    std::vector<NodeId> touched;

    Builder(const Graph& graph, const ContractionHierarchy::BuildOptions& o) : g(graph), opts(o) {
        const auto n = static_cast<std::size_t>(g.num_nodes());
        out.resize(n);
        in.resize(n);
        contracted.assign(n, 0);
        level.assign(n, 0);
        contracted_neighbours.assign(n, 0);
        dist.assign(n, kInf);
        hop.assign(n, 0);

        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                add_arc(u, g.edge_target(e), g.edge_weight(e), kInvalidNode);
            }
        }
    }

    /// Insert an arc, keeping only the cheapest for a given pair. Parallel
    /// edges are common in OSM data and would otherwise multiply through
    /// every contraction.
    void add_arc(NodeId u, NodeId v, double w, NodeId through) {
        if (u == v) return;
        const auto key = arc_key(u, v);
        auto it = best.find(key);
        if (it != best.end()) {
            if (w >= it->second) return;
            it->second = w;
            for (auto& a : out[u]) {
                if (a.to == v) {
                    a.weight = w;
                    a.via = through;
                }
            }
            for (auto& a : in[v]) {
                if (a.to == u) {
                    a.weight = w;
                    a.via = through;
                }
            }
            if (through != kInvalidNode)
                via[key] = through;
            else
                via.erase(key);
            return;
        }
        best.emplace(key, w);
        out[u].push_back({v, w, through});
        in[v].push_back({u, w, through});
        if (through != kInvalidNode) via[key] = through;
    }

    /// Is there a path u -> w avoiding `avoid` no longer than `limit`?
    ///
    /// Bounded by hops and by settled nodes: an exact answer would cost more
    /// than the contraction it is meant to save. Giving up early can only add
    /// a shortcut that turns out to be unnecessary, never change an answer.
    ///
    /// Always thorough. Using a cheaper bound for priority estimation is a
    /// false economy: every witness missed becomes a shortcut, and those
    /// shortcuts make every later contraction more expensive. Measured,
    /// halving the bound tripled total preprocessing time.
    bool witness_exists(NodeId u, NodeId w, NodeId avoid, double limit) {
        ++witness_searches;
        for (NodeId t : touched) dist[t] = kInf;
        touched.clear();
        pq_buf = MinHeap();

        dist[u] = 0.0;
        hop[u] = 0;
        touched.push_back(u);
        pq_buf.push({0.0, u});

        int settled = 0;
        while (!pq_buf.empty()) {
            const double d = pq_buf.top().key;
            const NodeId x = pq_buf.top().node;
            pq_buf.pop();
            if (d > dist[x]) continue;
            if (d > limit) return false;  // everything further is too long
            if (x == w) return true;
            if (++settled > opts.witness_node_limit) return false;
            const int h = hop[x];
            if (h >= opts.witness_hop_limit) continue;

            for (const auto& a : out[x]) {
                if (contracted[a.to] || a.to == avoid) continue;
                const double nd = d + a.weight;
                if (nd < dist[a.to] && nd <= limit) {
                    if (dist[a.to] == kInf) touched.push_back(a.to);
                    dist[a.to] = nd;
                    hop[a.to] = h + 1;
                    pq_buf.push({nd, a.to});
                }
            }
        }
        return false;
    }

    /// Shortcuts that contracting `v` would require. Applies them when
    /// `commit` is set; otherwise only counts, for the priority estimate.
    /// Estimation-only guard: a node with enormous degree already has a bad
    /// edge difference, so an approximate figure orders it correctly and a
    /// full count is not worth its cost.
    static constexpr int kEstimatePairCap = 2000;

    int process(NodeId v, bool commit) {
        int added = 0;
        int pairs = 0;
        for (const auto& iarc : in[v]) {
            const NodeId u = iarc.to;
            if (contracted[u]) continue;
            for (const auto& oarc : out[v]) {
                const NodeId w = oarc.to;
                if (contracted[w] || w == u) continue;
                if (!commit && ++pairs > kEstimatePairCap) return added + pairs;
                const double through = iarc.weight + oarc.weight;
                if (witness_exists(u, w, v, through)) continue;  // no worse route exists
                ++added;
                if (commit) {
                    add_arc(u, w, through, v);
                    ++shortcuts;
                }
            }
        }
        return added;
    }

    int degree(NodeId v) const {
        int d = 0;
        for (const auto& a : in[v]) d += contracted[a.to] ? 0 : 1;
        for (const auto& a : out[v]) d += contracted[a.to] ? 0 : 1;
        return d;
    }

    /// Lower is contracted sooner. Edge difference is the dominant term:
    /// contract nodes that create fewer shortcuts than they remove edges.
    /// The other two terms spread the contraction out spatially, which keeps
    /// the hierarchy from degenerating in dense city centres.
    double priority(NodeId v) {
        const int shortcuts_needed = process(v, /*commit=*/false);
        const int edge_diff = shortcuts_needed - degree(v);
        return opts.edge_difference_weight * edge_diff +
               opts.contracted_neighbours_weight * contracted_neighbours[v] +
               opts.original_edges_weight * static_cast<int>(out[v].size() + in[v].size()) / 4;
    }
};

void ContractionHierarchy::build(const Graph& g, const BuildOptions& opts) {
    const auto t0 = Clock::now();
    graph_ = &g;
    built_ = false;
    stats_ = Stats{};

    const auto n = static_cast<std::size_t>(g.num_nodes());
    up_.assign(n, {});
    down_.assign(n, {});
    level_.assign(n, 0);
    if (n == 0) {
        built_ = true;
        return;
    }

    Builder b(g, opts);

    MinHeap pq;
    for (NodeId v = 0; v < g.num_nodes(); ++v) pq.push({b.priority(v), v});

    std::int32_t rank = 0;
    const auto deadline = t0 + std::chrono::duration_cast<Clock::duration>(
                                   std::chrono::duration<double, std::milli>(opts.budget_ms));
    int since_clock_check = 0;
    while (!pq.empty()) {
        // Checking the clock every pop would itself be measurable.
        if (++since_clock_check >= 64) {
            since_clock_check = 0;
            if (Clock::now() > deadline) {
                stats_.aborted = true;
                break;
            }
        }
        const NodeId v = pq.top().node;
        const double key = pq.top().key;
        pq.pop();
        if (b.contracted[v]) continue;

        // Lazy update: the stored key may be stale because neighbours were
        // contracted since it was computed. Recheck against the current best
        // and re-queue if this node is no longer the cheapest.
        const double fresh = b.priority(v);
        if (!pq.empty() && fresh > pq.top().key && fresh > key) {
            pq.push({fresh, v});
            continue;
        }

        b.process(v, /*commit=*/true);
        b.contracted[v] = 1;
        b.level[v] = rank++;

        for (const auto& a : b.out[v]) b.contracted_neighbours[a.to]++;
        for (const auto& a : b.in[v]) b.contracted_neighbours[a.to]++;
    }

    // Anything left uncontracted forms a "core" that never had its shortcuts
    // computed. Ranking it above the contracted nodes is not enough on its
    // own: an up-only search can traverse a core arc in one direction only,
    // so pairs inside the core become unreachable. Core arcs are therefore
    // given to both searches below, which turns the core back into an
    // ordinary bidirectional Dijkstra.
    std::vector<char> in_core(static_cast<std::size_t>(g.num_nodes()), 0);
    if (stats_.aborted) {
        for (NodeId v = 0; v < g.num_nodes(); ++v) {
            if (!b.contracted[v]) {
                b.level[v] = rank++;
                in_core[v] = 1;
            }
        }
    }
    level_ = b.level;
    stats_.max_level = rank ? rank - 1 : 0;
    stats_.shortcuts = b.shortcuts;
    stats_.original_edges = g.num_edges();
    stats_.witness_searches = b.witness_searches;

    // Split the augmented graph by direction through the hierarchy. A forward
    // search only ever climbs; a backward search climbs the reversed arcs.
    std::int64_t total_arcs = 0;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (const auto& a : b.out[u]) {
            ++total_arcs;
            if (in_core[u] && in_core[a.to]) {
                // Both directions, so the core stays fully searchable.
                up_[u].push_back({a.to, a.weight, a.via});
                down_[a.to].push_back({u, a.weight, a.via});
            } else if (level_[a.to] > level_[u]) {
                up_[u].push_back({a.to, a.weight, a.via});
            } else {
                down_[a.to].push_back({u, a.weight, a.via});
            }
        }
    }
    stats_.edge_growth = g.num_edges() > 0 ? static_cast<double>(total_arcs) / g.num_edges() : 1.0;

    via_.clear();
    via_.reserve(b.via.size());
    for (const auto& [k, v] : b.via) via_.emplace(k, v);

    stats_.build_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    built_ = true;
}

void ContractionHierarchy::unpack(NodeId from, NodeId to, double weight,
                                  std::vector<NodeId>& out) const {
    auto it = via_.find(arc_key(from, to));
    if (it == via_.end()) {
        out.push_back(to);  // original edge
        return;
    }
    const NodeId mid = it->second;
    unpack(from, mid, weight, out);
    unpack(mid, to, weight, out);
}

SearchResult ContractionHierarchy::query(NodeId source, NodeId target,
                                         const SearchOptions& opts) const {
    SearchResult res;
    res.algorithm = "Contraction Hierarchies";
    res.time_complexity = "O(k log k), k << V";
    res.space_complexity = "O(V + E + shortcuts)";
    if (!built_ || graph_ == nullptr) return res;
    const auto n = graph_->num_nodes();
    if (source < 0 || target < 0 || source >= n || target >= n) return res;
    if (source == target) {
        res.success = true;
        res.path = {source};
        return res;
    }

    const auto t0 = Clock::now();
    std::vector<double> df(n, kInf), db(n, kInf);
    std::vector<NodeId> pf(n, kInvalidNode), pb(n, kInvalidNode);
    std::vector<char> sf(n, 0), sb(n, 0);
    MinHeap qf, qb;

    df[source] = 0.0;
    db[target] = 0.0;
    qf.push({0.0, source});
    qb.push({0.0, target});
    if (opts.trace) {
        opts.trace->discover(source, kInvalidNode);
        opts.trace->discover(target, kInvalidNode);
    }

    double mu = kInf;
    NodeId meet = kInvalidNode;

    // Both searches run to exhaustion of their own upward region rather than
    // stopping at the first meeting: the shortest path may still improve
    // while either frontier is below the best total found so far.
    while (!qf.empty() || !qb.empty()) {
        if (!qf.empty() && qf.top().key <= mu) {
            const NodeId u = qf.top().node;
            const double d = qf.top().key;
            qf.pop();
            if (!sf[u] && d <= df[u]) {
                sf[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                if (db[u] != kInf && df[u] + db[u] < mu) {
                    mu = df[u] + db[u];
                    meet = u;
                }
                for (const auto& a : up_[u]) {
                    ++res.edges_relaxed;
                    const double nd = df[u] + a.weight;
                    if (nd < df[a.to]) {
                        const bool first = df[a.to] == kInf;
                        df[a.to] = nd;
                        pf[a.to] = u;
                        qf.push({nd, a.to});
                        if (opts.trace) {
                            first ? opts.trace->discover(a.to, u) : opts.trace->relax(a.to, u);
                        }
                    }
                }
            }
        } else if (!qb.empty() && qb.top().key <= mu) {
            const NodeId u = qb.top().node;
            const double d = qb.top().key;
            qb.pop();
            if (!sb[u] && d <= db[u]) {
                sb[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                if (df[u] != kInf && df[u] + db[u] < mu) {
                    mu = df[u] + db[u];
                    meet = u;
                }
                for (const auto& a : down_[u]) {
                    ++res.edges_relaxed;
                    const double nd = db[u] + a.weight;
                    if (nd < db[a.to]) {
                        const bool first = db[a.to] == kInf;
                        db[a.to] = nd;
                        pb[a.to] = u;
                        qb.push({nd, a.to});
                        if (opts.trace) {
                            first ? opts.trace->discover(a.to, u) : opts.trace->relax(a.to, u);
                        }
                    }
                }
            }
        } else {
            break;
        }
    }
    res.algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (meet == kInvalidNode || mu == kInf) return res;

    // Walk both halves in hierarchy space, expanding shortcuts as we go.
    std::vector<NodeId> up_half;
    for (NodeId c = meet; c != kInvalidNode && c != source; c = pf[c]) up_half.push_back(c);
    std::reverse(up_half.begin(), up_half.end());

    res.path.push_back(source);
    NodeId prev = source;
    for (NodeId nxt : up_half) {
        unpack(prev, nxt, 0.0, res.path);
        prev = nxt;
    }
    for (NodeId c = pb[meet]; c != kInvalidNode; c = pb[c]) {
        unpack(prev, c, 0.0, res.path);
        prev = c;
        if (c == target) break;
    }

    if (!res.path.empty() && res.path.front() == source && res.path.back() == target) {
        res.success = true;
        res.path_cost = mu;
    } else {
        res.path.clear();
    }
    return res;
}

/// Registry adapter.
///
/// CH does not fit the stateless Algorithm shape: it needs a preprocessing
/// pass whose cost only pays off across many queries. Rather than rebuild per
/// call, an instance remembers which graph it prepared for and reuses the
/// hierarchy -- so race mode and the benchmark harness, which hold one
/// instance and query repeatedly, pay for it once.
class CHAlgorithm : public Algorithm {
public:
    std::string name() const override { return "Contraction Hierarchies"; }
    std::string time_complexity() const override { return "O(k log k), k << V"; }
    std::string space_complexity() const override { return "O(V + E + shortcuts)"; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        if (opts.closures != nullptr && !opts.closures->empty()) {
            // Shortcuts were built against the full graph, so a closure would
            // silently route through a shut road. Declining is the honest
            // answer; re-preprocessing per closure defeats the purpose.
            SearchResult res = make_result();
            res.algorithm = name() + " (skipped: closures need re-preprocessing)";
            return res;
        }
        if (prepared_for_ != &g) {
            ch_.build(g);
            prepared_for_ = &g;
        }
        auto res = ch_.query(source, target, opts);
        res.algorithm = name();
        return res;
    }

    const ContractionHierarchy& hierarchy() const { return ch_; }

private:
    mutable ContractionHierarchy ch_;
    mutable const Graph* prepared_for_ = nullptr;
};

void register_contraction(Registry& r) {
    r.add("ch", [] { return std::unique_ptr<Algorithm>(new CHAlgorithm()); });
}

}  // namespace agss
