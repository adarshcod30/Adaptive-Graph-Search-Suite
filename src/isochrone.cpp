#include "agss/isochrone.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>

namespace agss {
namespace {
using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Entry {
    double key;
    NodeId node;
    bool operator>(const Entry& o) const { return key > o.key; }
};

double cross(const std::pair<double, double>& o, const std::pair<double, double>& a,
             const std::pair<double, double>& b) {
    return (a.first - o.first) * (b.second - o.second) -
           (a.second - o.second) * (b.first - o.first);
}
}  // namespace

std::vector<std::pair<double, double>> convex_hull(std::vector<std::pair<double, double>> pts) {
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    const std::size_t n = pts.size();
    if (n < 3) return pts;

    std::vector<std::pair<double, double>> hull(2 * n);
    std::size_t k = 0;
    for (std::size_t i = 0; i < n; ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = n - 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);
    return hull;
}

IsochroneReport isochrone(const Graph& g, NodeId origin, const std::vector<double>& cutoffs,
                          const SearchOptions& opts) {
    IsochroneReport rep;
    const auto t0 = Clock::now();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    rep.cost_to.assign(n, kInf);
    if (origin < 0 || origin >= g.num_nodes() || cutoffs.empty()) {
        rep.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return rep;
    }

    const double max_cut = *std::max_element(cutoffs.begin(), cutoffs.end());

    std::vector<char> settled(n, 0);
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;
    rep.cost_to[static_cast<std::size_t>(origin)] = 0.0;
    pq.push({0.0, origin});
    if (opts.trace) opts.trace->discover(origin, kInvalidNode);

    while (!pq.empty()) {
        const auto [d, u] = std::pair{pq.top().key, pq.top().node};
        pq.pop();
        if (settled[u]) continue;
        if (d > max_cut) break;  // everything further is outside every band
        settled[u] = 1;
        if (opts.trace) opts.trace->expand(u);

        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (!opts.edge_open(e)) continue;
            const NodeId v = g.edge_target(e);
            const double nd = rep.cost_to[static_cast<std::size_t>(u)] + g.edge_weight(e);
            if (nd < rep.cost_to[static_cast<std::size_t>(v)] && nd <= max_cut) {
                const bool first = rep.cost_to[static_cast<std::size_t>(v)] == kInf;
                rep.cost_to[static_cast<std::size_t>(v)] = nd;
                pq.push({nd, v});
                if (opts.trace) first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
            }
        }
    }

    std::vector<double> sorted = cutoffs;
    std::sort(sorted.begin(), sorted.end());
    for (double cut : sorted) {
        IsochroneBand band;
        band.cutoff = cut;
        std::vector<std::pair<double, double>> pts;
        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            if (rep.cost_to[static_cast<std::size_t>(u)] <= cut) {
                band.nodes.push_back(u);
                pts.emplace_back(g.x(u), g.y(u));
            }
        }
        band.hull = convex_hull(std::move(pts));
        rep.bands.push_back(std::move(band));
    }

    rep.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return rep;
}

}  // namespace agss
