#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "agss/error.hpp"
#include "agss/graph.hpp"

namespace agss {

/// Builds a CSR Graph from an edge/node list, validating as it goes.
///
/// Validation is the whole point. The previous loader accepted an edge whose
/// endpoint was never declared as a node; downstream that produced three
/// separate failures from one root cause -- Greedy aborted on
/// `nodes.at(missing)`, A* silently skipped the edge because `gScore[missing]`
/// default-constructed to 0, and Floyd-Warshall emitted a path through the
/// phantom node while reporting success. Rejecting the edge at load time
/// removes all three at once.
class GraphBuilder {
public:
    explicit GraphBuilder(CoordSpace space = CoordSpace::Planar) : space_(space) {}

    /// Declare a node. Duplicate external ids keep the first coordinate seen.
    NodeId add_node(std::int64_t external_id, double x, double y);

    /// Record a directed edge. Endpoints must already be declared.
    /// Rejects NaN/inf and (unless `allow_negative`) negative weights.
    Result<std::monostate> add_edge(std::int64_t from, std::int64_t to, double weight,
                                    bool allow_negative = false);

    /// Undirected convenience: adds both directions.
    Result<std::monostate> add_undirected(std::int64_t a, std::int64_t b, double weight,
                                          bool allow_negative = false);

    bool has_node(std::int64_t external_id) const { return index_.count(external_id) > 0; }
    std::size_t node_count() const { return external_ids_.size(); }
    std::size_t edge_count() const { return pending_.size(); }

    /// Finalise into CSR order. Consumes the builder's buffers.
    Graph build();

private:
    struct PendingEdge {
        NodeId from;
        NodeId to;
        double weight;
    };

    std::vector<PendingEdge> pending_;
    std::vector<double> xs_, ys_;
    std::vector<std::int64_t> external_ids_;
    std::unordered_map<std::int64_t, NodeId> index_;
    CoordSpace space_;
};

}  // namespace agss
