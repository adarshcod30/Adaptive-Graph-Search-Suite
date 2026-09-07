#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "agss/graph.hpp"

namespace agss {

/// What the search did, one step at a time.
enum class Op : std::uint8_t {
    Discover = 0,  ///< node entered the frontier, with a parent
    Expand = 1,    ///< node left the frontier and was settled
    Relax = 2,     ///< a cheaper parent was found for a node already in the frontier
};

struct Event {
    Op op;
    NodeId node;
    NodeId parent;  ///< kInvalidNode for Expand
};

/// Append-only record of a search, stored as deltas rather than snapshots.
///
/// The original format wrote, for every frame, the *complete* frontier,
/// explored set and current path. That is Theta(V^2) output: measured at
/// 30 MB for 3.6k nodes, 239 MB at 10k and 980 MB at 19.6k -- which
/// extrapolates to roughly 57 GB for a single query on a real 150k-node city,
/// and is the reason the engine could not leave toy-sized graphs.
///
/// Storing deltas instead makes the trace Theta(V + E): each node is
/// discovered once, expanded once, and relaxed at most once per incoming edge.
/// The client replays events to rebuild any frame it wants, so nothing is lost.
class Trace {
public:
    void discover(NodeId node, NodeId parent) { events_.push_back({Op::Discover, node, parent}); }
    void expand(NodeId node) { events_.push_back({Op::Expand, node, kInvalidNode}); }
    void relax(NodeId node, NodeId parent) { events_.push_back({Op::Relax, node, parent}); }

    void reserve(std::size_t n) { events_.reserve(n); }
    const std::vector<Event>& events() const noexcept { return events_; }
    std::size_t size() const noexcept { return events_.size(); }
    bool empty() const noexcept { return events_.empty(); }
    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

}  // namespace agss
