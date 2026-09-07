#include "agss/kdtree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace agss {

KdTree::KdTree(const Graph& g) : graph_(&g) {
    nodes_.reserve(static_cast<std::size_t>(g.num_nodes()));
    for (NodeId u = 0; u < g.num_nodes(); ++u) nodes_.push_back({g.x(u), g.y(u), u});
    if (!nodes_.empty()) build(0, nodes_.size(), 0);
}

double KdTree::metric(double ax, double ay, double bx, double by) const {
    if (graph_ && graph_->coord_space() == CoordSpace::Geographic) {
        return geo::haversine(ay, ax, by, bx);  // y=lat, x=lon; matches Graph::straight_line
    }
    return geo::euclidean(ax, ay, bx, by);
}

void KdTree::build(std::size_t lo, std::size_t hi, int depth) {
    if (hi - lo <= 1) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    const bool by_x = (depth % 2) == 0;
    std::nth_element(nodes_.begin() + static_cast<std::ptrdiff_t>(lo),
                     nodes_.begin() + static_cast<std::ptrdiff_t>(mid),
                     nodes_.begin() + static_cast<std::ptrdiff_t>(hi),
                     [by_x](const Item& a, const Item& b) { return by_x ? a.x < b.x : a.y < b.y; });
    build(lo, mid, depth + 1);
    build(mid + 1, hi, depth + 1);
}

void KdTree::nearest_recurse(std::size_t lo, std::size_t hi, int depth, double x, double y,
                             NodeId& best, double& best_d) const {
    if (lo >= hi) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    const Item& piv = nodes_[mid];

    const double d = metric(x, y, piv.x, piv.y);
    if (d < best_d) {
        best_d = d;
        best = piv.id;
    }
    if (hi - lo == 1) return;

    const bool by_x = (depth % 2) == 0;
    const double delta = by_x ? x - piv.x : y - piv.y;
    const std::size_t near_lo = delta < 0 ? lo : mid + 1;
    const std::size_t near_hi = delta < 0 ? mid : hi;
    const std::size_t far_lo = delta < 0 ? mid + 1 : lo;
    const std::size_t far_hi = delta < 0 ? hi : mid;

    nearest_recurse(near_lo, near_hi, depth + 1, x, y, best, best_d);

    // Only descend the far side if the splitting plane could hide something
    // closer. Compare in the graph's own metric so the geographic case stays
    // sound rather than mixing degrees with metres.
    const double plane = by_x ? metric(x, y, piv.x, y) : metric(x, y, x, piv.y);
    if (plane < best_d) nearest_recurse(far_lo, far_hi, depth + 1, x, y, best, best_d);
}

NodeId KdTree::nearest(double x, double y) const {
    if (nodes_.empty()) return kInvalidNode;
    NodeId best = kInvalidNode;
    double best_d = std::numeric_limits<double>::infinity();
    nearest_recurse(0, nodes_.size(), 0, x, y, best, best_d);
    return best;
}

void KdTree::within_recurse(std::size_t lo, std::size_t hi, int depth, double x, double y,
                            double radius, std::vector<NodeId>& out) const {
    if (lo >= hi) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    const Item& piv = nodes_[mid];
    if (metric(x, y, piv.x, piv.y) <= radius) out.push_back(piv.id);
    if (hi - lo == 1) return;

    const bool by_x = (depth % 2) == 0;
    const double delta = by_x ? x - piv.x : y - piv.y;
    const double plane = by_x ? metric(x, y, piv.x, y) : metric(x, y, x, piv.y);

    if (delta < 0) {
        within_recurse(lo, mid, depth + 1, x, y, radius, out);
        if (plane <= radius) within_recurse(mid + 1, hi, depth + 1, x, y, radius, out);
    } else {
        within_recurse(mid + 1, hi, depth + 1, x, y, radius, out);
        if (plane <= radius) within_recurse(lo, mid, depth + 1, x, y, radius, out);
    }
}

std::vector<NodeId> KdTree::within(double x, double y, double radius) const {
    std::vector<NodeId> out;
    if (!nodes_.empty()) within_recurse(0, nodes_.size(), 0, x, y, radius, out);
    return out;
}

}  // namespace agss
