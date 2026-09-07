#pragma once

#include <vector>

#include "agss/graph.hpp"

namespace agss {

/// Static 2-d tree over the graph's nodes, for "snap this coordinate to the
/// nearest intersection" queries.
///
/// Without a spatial index every click costs a linear scan of the node array.
/// That is invisible on a 400-node toy graph and painful on a real city of
/// 150k+ nodes, where it is also on the interactive path. Build is
/// O(V log V); a query is O(log V) expected.
class KdTree {
public:
    KdTree() = default;
    explicit KdTree(const Graph& g);

    /// Nearest node to (x, y), or kInvalidNode if the tree is empty.
    /// Coordinates are interpreted in the graph's own space, and geographic
    /// distances are compared with the graph's metric so latitude convergence
    /// is handled correctly.
    NodeId nearest(double x, double y) const;

    /// All nodes within `radius` of (x, y), in the graph's distance unit.
    std::vector<NodeId> within(double x, double y, double radius) const;

    bool empty() const noexcept { return nodes_.empty(); }
    std::size_t size() const noexcept { return nodes_.size(); }

private:
    struct Item {
        double x, y;
        NodeId id;
    };

    void build(std::size_t lo, std::size_t hi, int depth);
    void nearest_recurse(std::size_t lo, std::size_t hi, int depth, double x, double y,
                         NodeId& best, double& best_d) const;
    void within_recurse(std::size_t lo, std::size_t hi, int depth, double x, double y,
                        double radius, std::vector<NodeId>& out) const;
    double metric(double ax, double ay, double bx, double by) const;

    std::vector<Item> nodes_;
    const Graph* graph_ = nullptr;
};

}  // namespace agss
