#include "agss/algorithm.hpp"

#include <algorithm>
#include <unordered_set>

namespace agss {

std::size_t ClosureMask::close_road(const Graph& g, NodeId u, NodeId v) {
    std::size_t n = 0;
    for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
        if (g.edge_target(e) == v) {
            close(e);
            ++n;
        }
    }
    for (EdgeId e = g.edge_begin(v); e < g.edge_end(v); ++e) {
        if (g.edge_target(e) == u) {
            close(e);
            ++n;
        }
    }
    return n;
}

Registry& Registry::instance() {
    static Registry r = [] {
        Registry reg;
        register_uninformed(reg);
        register_weighted(reg);
        register_global(reg);
        register_contraction(reg);
        register_alt(reg);
        return reg;
    }();
    return r;
}

void Registry::add(std::string key, std::unique_ptr<Algorithm> (*factory)()) {
    entries_.emplace_back(std::move(key), factory);
}

std::unique_ptr<Algorithm> Registry::create(const std::string& key) const {
    for (const auto& [k, f] : entries_) {
        if (k == key) return f();
    }
    return nullptr;
}

bool Registry::contains(const std::string& key) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const auto& e) { return e.first == key; });
}

std::vector<std::string> Registry::keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& [k, f] : entries_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<NodeId> reconstruct_path(const std::vector<NodeId>& parent, NodeId source,
                                     NodeId target) {
    std::vector<NodeId> path;
    if (source == target) return {source};
    if (target < 0 || static_cast<std::size_t>(target) >= parent.size()) return {};

    // Bounded by the node count: a parent chain longer than V has a cycle, so
    // we bail instead of spinning. The old reconstruction loops had no such
    // guard and could hang on a corrupt chain.
    std::unordered_set<NodeId> seen;
    NodeId curr = target;
    for (std::size_t steps = 0; steps <= parent.size(); ++steps) {
        path.push_back(curr);
        if (curr == source) {
            std::reverse(path.begin(), path.end());
            return path;
        }
        if (!seen.insert(curr).second) return {};  // cycle
        curr = parent[curr];
        if (curr == kInvalidNode) return {};
    }
    return {};
}

double path_cost(const Graph& g, const std::vector<NodeId>& path) {
    if (path.size() < 2) return path.empty() ? -1.0 : 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const NodeId u = path[i];
        const NodeId v = path[i + 1];
        double best = -1.0;
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (g.edge_target(e) == v) {
                const double w = g.edge_weight(e);
                if (best < 0.0 || w < best) best = w;
            }
        }
        if (best < 0.0) return -1.0;  // not a real edge
        total += best;
    }
    return total;
}

}  // namespace agss
