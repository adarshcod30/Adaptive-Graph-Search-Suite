#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "agss/graph.hpp"

namespace agss::analysis {

struct Bridge {
    NodeId u, v;
    /// How many nodes get cut off from the larger side if this road closes.
    std::int64_t isolated_nodes = 0;
};

struct ConnectivityReport {
    std::vector<Bridge> bridges;                ///< edges whose removal disconnects
    std::vector<NodeId> articulation_points;    ///< nodes whose removal disconnects
    std::int64_t component_count = 0;
    double elapsed_ms = 0.0;
};

/// Tarjan's bridge + articulation point search over the undirected projection.
///
/// The urban question: which single road closure severs part of the city?
/// A bridge is an edge on no cycle, so there is no alternate route around it.
ConnectivityReport find_bridges(const Graph& g);

struct SccReport {
    std::vector<NodeId> component_of;  ///< size V, component index per node
    std::int64_t count = 0;
    std::vector<std::int64_t> sizes;
    double elapsed_ms = 0.0;
};

/// Tarjan's strongly connected components, iterative so deep graphs cannot
/// blow the stack. On a road network this exposes one-way systems that trap
/// traffic in a region it cannot legally leave.
SccReport strongly_connected_components(const Graph& g);

struct CentralityReport {
    std::vector<double> betweenness;  ///< size V
    double elapsed_ms = 0.0;
    std::int64_t sources_sampled = 0;
    bool exact = true;
};

/// Brandes' betweenness centrality: the share of shortest paths that pass
/// through each node. On a road graph this is a direct proxy for through
/// traffic, and it is what turns a routing engine into a congestion map.
///
/// Exact cost is O(V*E) on weighted graphs, which is too slow above a few
/// thousand nodes, so `sample_sources > 0` estimates from a random subset --
/// the standard approximation, with error that falls as 1/sqrt(samples).
CentralityReport betweenness_centrality(const Graph& g, std::int64_t sample_sources = 0,
                                        std::uint64_t seed = 42);

struct SpanningTreeReport {
    std::vector<std::pair<NodeId, NodeId>> edges;
    double total_weight = 0.0;
    bool spans_all_nodes = false;
    double elapsed_ms = 0.0;
};

/// Kruskal's MST with union-find (path compression + union by rank).
/// "Cheapest road network that still connects every ward."
SpanningTreeReport minimum_spanning_tree(const Graph& g);

struct FlowReport {
    double max_flow = 0.0;
    /// Edges crossing the minimum cut -- the bottleneck set.
    std::vector<std::pair<NodeId, NodeId>> min_cut;
    double elapsed_ms = 0.0;
};

/// Dinic's max-flow. Edge weights are read as capacities, so this answers
/// "how much traffic per hour can actually move from A to B", and the min cut
/// names the roads that constrain it.
FlowReport max_flow(const Graph& g, NodeId source, NodeId sink);

}  // namespace agss::analysis
