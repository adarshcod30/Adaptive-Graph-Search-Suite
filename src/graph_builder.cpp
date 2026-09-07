#include "agss/graph_builder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace agss {

NodeId GraphBuilder::add_node(std::int64_t external_id, double x, double y) {
    auto it = index_.find(external_id);
    if (it != index_.end()) return it->second;

    const auto id = static_cast<NodeId>(external_ids_.size());
    index_.emplace(external_id, id);
    external_ids_.push_back(external_id);
    xs_.push_back(x);
    ys_.push_back(y);
    return id;
}

Result<std::monostate> GraphBuilder::add_edge(std::int64_t from, std::int64_t to, double weight,
                                              bool allow_negative) {
    auto fit = index_.find(from);
    if (fit == index_.end()) {
        return Error{"edge references undeclared node " + std::to_string(from)};
    }
    auto tit = index_.find(to);
    if (tit == index_.end()) {
        return Error{"edge references undeclared node " + std::to_string(to)};
    }
    if (!std::isfinite(weight)) {
        return Error{"edge " + std::to_string(from) + "->" + std::to_string(to) +
                     " has non-finite weight"};
    }
    if (weight < 0.0 && !allow_negative) {
        return Error{"edge " + std::to_string(from) + "->" + std::to_string(to) +
                     " has negative weight " + std::to_string(weight) +
                     " (only Bellman-Ford and Johnson accept these)"};
    }
    pending_.push_back({fit->second, tit->second, weight});
    return std::monostate{};
}

Result<std::monostate> GraphBuilder::add_undirected(std::int64_t a, std::int64_t b, double weight,
                                                    bool allow_negative) {
    auto first = add_edge(a, b, weight, allow_negative);
    if (!first) return first;
    return add_edge(b, a, weight, allow_negative);
}

Graph GraphBuilder::build() {
    Graph g;
    const auto n = static_cast<NodeId>(external_ids_.size());

    g.space_ = space_;
    g.xs_ = std::move(xs_);
    g.ys_ = std::move(ys_);
    g.external_ids_ = std::move(external_ids_);
    g.index_ = std::move(index_);

    // Counting sort by source node: one pass to count degrees, prefix-sum into
    // offsets, second pass to scatter. O(V + E), no comparison sort needed.
    g.offsets_.assign(static_cast<std::size_t>(n) + 1, 0);
    for (const auto& e : pending_) g.offsets_[e.from + 1]++;
    std::partial_sum(g.offsets_.begin(), g.offsets_.end(), g.offsets_.begin());

    g.targets_.resize(pending_.size());
    g.weights_.resize(pending_.size());

    std::vector<EdgeId> cursor(g.offsets_.begin(), g.offsets_.end() - 1);
    double min_w = std::numeric_limits<double>::infinity();
    for (const auto& e : pending_) {
        const EdgeId slot = cursor[e.from]++;
        g.targets_[slot] = e.to;
        g.weights_[slot] = e.weight;
        if (e.weight > 0.0) min_w = std::min(min_w, e.weight);
    }
    g.min_weight_ = std::isfinite(min_w) ? min_w : 1.0;

    pending_.clear();
    pending_.shrink_to_fit();
    return g;
}

double Graph::heuristic_admissibility() const noexcept {
    double worst = std::numeric_limits<double>::infinity();
    for (NodeId u = 0; u < num_nodes(); ++u) {
        for (EdgeId e = edge_begin(u); e < edge_end(u); ++e) {
            const NodeId v = edge_target(e);
            const double straight = straight_line(u, v);
            if (straight <= 1e-12) continue;  // coincident endpoints tell us nothing
            worst = std::min(worst, edge_weight(e) / straight);
        }
    }
    return std::isfinite(worst) ? worst : 1.0;
}

}  // namespace agss
