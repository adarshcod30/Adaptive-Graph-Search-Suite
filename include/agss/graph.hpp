#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "agss/geo.hpp"

namespace agss {

/// Dense, contiguous node index in [0, num_nodes). Distinct from the external
/// id that appeared in the source data.
using NodeId = std::int32_t;
using EdgeId = std::int32_t;

inline constexpr NodeId kInvalidNode = -1;

/// How to interpret node coordinates.
enum class CoordSpace {
    Planar,      ///< (x, y) in arbitrary units. Euclidean metric.
    Geographic,  ///< (lat, lon) in WGS84 degrees. Great-circle metric.
};

struct Neighbor {
    NodeId target;
    double weight;
};

/// Immutable graph in Compressed Sparse Row layout.
///
/// The original engine stored `unordered_map<int, vector<Edge>>`, which makes
/// every neighbour lookup a hash probe followed by a pointer chase into a
/// separately heap-allocated vector. CSR instead keeps two flat arrays, so the
/// neighbours of a node are one contiguous run of memory:
///
///     neighbours(u) == targets_[offsets_[u] .. offsets_[u + 1])
///
/// That is a sequential scan the prefetcher can follow, which is where the
/// speedup comes from on graphs too large to sit in cache. The trade-off is
/// immutability -- you cannot add an edge after construction -- which is
/// exactly right for a road network loaded once and queried many times.
/// Build one with GraphBuilder.
class Graph {
public:
    Graph() = default;

    NodeId num_nodes() const noexcept { return static_cast<NodeId>(offsets_.size()) - 1; }
    EdgeId num_edges() const noexcept { return static_cast<EdgeId>(targets_.size()); }
    CoordSpace coord_space() const noexcept { return space_; }

    /// Contiguous view of a node's outgoing edges. No allocation, no copy.
    std::span<const NodeId> targets(NodeId u) const noexcept {
        return {targets_.data() + offsets_[u], static_cast<std::size_t>(degree(u))};
    }
    std::span<const double> weights(NodeId u) const noexcept {
        return {weights_.data() + offsets_[u], static_cast<std::size_t>(degree(u))};
    }
    EdgeId degree(NodeId u) const noexcept { return offsets_[u + 1] - offsets_[u]; }

    /// Index of the first outgoing edge of `u`, for algorithms that need to
    /// address edges globally (e.g. edge-flag or closure masks).
    EdgeId edge_begin(NodeId u) const noexcept { return offsets_[u]; }
    EdgeId edge_end(NodeId u) const noexcept { return offsets_[u + 1]; }
    NodeId edge_target(EdgeId e) const noexcept { return targets_[e]; }
    double edge_weight(EdgeId e) const noexcept { return weights_[e]; }

    double x(NodeId u) const noexcept { return xs_[u]; }  ///< lon when Geographic
    double y(NodeId u) const noexcept { return ys_[u]; }  ///< lat when Geographic
    double lon(NodeId u) const noexcept { return xs_[u]; }
    double lat(NodeId u) const noexcept { return ys_[u]; }

    /// External id this node had in the source data.
    std::int64_t external_id(NodeId u) const noexcept { return external_ids_[u]; }

    /// Dense index for an external id, or kInvalidNode if absent.
    NodeId lookup(std::int64_t external) const noexcept {
        auto it = index_.find(external);
        return it == index_.end() ? kInvalidNode : it->second;
    }

    /// Straight-line distance between two nodes under the graph's metric.
    /// This is the A* / Greedy heuristic, and is admissible provided edge
    /// weights are real distances in the same unit.
    /// Haversine, not the cheaper equirectangular approximation, is used on
    /// geographic graphs on purpose. The OSM importer measures road length
    /// with haversine, so a haversine heuristic is a true lower bound by the
    /// triangle inequality. Equirectangular can exceed it by up to ~0.5% over
    /// city extents, which is enough to make the estimate an *over*-estimate
    /// on short edges and quietly cost A* its optimality guarantee -- measured
    /// at 0.995x on the real Delhi extract. Correctness wins over the couple
    /// of trig operations saved.
    double straight_line(NodeId a, NodeId b) const noexcept {
        if (space_ == CoordSpace::Geographic) {
            return geo::haversine(ys_[a], xs_[a], ys_[b], xs_[b]);
        }
        return geo::euclidean(xs_[a], ys_[a], xs_[b], ys_[b]);
    }

    /// Smallest edge weight in the graph. Needed to keep a heuristic
    /// admissible when weights are travel times rather than distances.
    double min_edge_weight() const noexcept { return min_weight_; }

    /// Smallest ratio of edge weight to the straight-line distance between its
    /// endpoints, over the whole graph. Reported for diagnostics.
    double heuristic_admissibility() const noexcept;

    /// Largest amount, in the graph's own distance unit, by which an edge
    /// weight falls below the straight-line distance between its endpoints.
    /// Zero when none do.
    ///
    /// This is the number that decides whether A* and Greedy can be trusted.
    /// An edge weighing less than the crow-flies distance turns the heuristic
    /// into an *over*-estimate and costs A* its optimality guarantee -- the
    /// map generator once emitted edges at 0.9x the straight-line distance,
    /// which is exactly how that surfaced.
    ///
    /// The ratio alone is the wrong test: on real OSM data a 0.68 m service
    /// road losing 4 mm to CSV rounding scores 0.995, while a genuinely broken
    /// weight on a kilometre-long arterial might score 0.999. Absolute
    /// shortfall separates rounding noise from a real modelling error.
    double worst_heuristic_shortfall() const noexcept;

    /// True when a straight-line heuristic can be trusted on this graph.
    /// `tolerance` is an absolute slack for serialisation rounding: 1 cm when
    /// weights are metres.
    bool heuristic_is_admissible(double tolerance = 0.01) const noexcept {
        return worst_heuristic_shortfall() <= tolerance;
    }

private:
    friend class GraphBuilder;

    std::vector<EdgeId> offsets_;  // size V + 1
    std::vector<NodeId> targets_;  // size E
    std::vector<double> weights_;  // size E
    std::vector<double> xs_, ys_;  // size V
    std::vector<std::int64_t> external_ids_;
    std::unordered_map<std::int64_t, NodeId> index_;
    CoordSpace space_ = CoordSpace::Planar;
    double min_weight_ = 1.0;
};

}  // namespace agss
