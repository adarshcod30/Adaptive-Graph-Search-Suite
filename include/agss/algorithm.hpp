#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agss/graph.hpp"
#include "agss/search_result.hpp"
#include "agss/trace.hpp"

namespace agss {

/// Per-edge open/closed mask, indexed by EdgeId. Backs the "what-if a road
/// shuts" feature: close a set of edges and re-run any algorithm unchanged.
class ClosureMask {
public:
    ClosureMask() = default;
    explicit ClosureMask(EdgeId num_edges) : closed_(static_cast<std::size_t>(num_edges), false) {}

    void close(EdgeId e) { closed_[e] = true; }
    void open(EdgeId e) { closed_[e] = false; }
    bool is_closed(EdgeId e) const noexcept { return closed_[e]; }
    bool empty() const noexcept { return closed_.empty(); }
    std::size_t count_closed() const noexcept {
        std::size_t n = 0;
        for (bool b : closed_) n += b ? 1 : 0;
        return n;
    }

    /// Close both directions of the undirected road between u and v.
    std::size_t close_road(const Graph& g, NodeId u, NodeId v);

private:
    std::vector<bool> closed_;
};

struct SearchOptions {
    /// When null the search records nothing. `--bench` uses this to measure
    /// the algorithm with zero observer overhead.
    Trace* trace = nullptr;
    const ClosureMask* closures = nullptr;

    bool edge_open(EdgeId e) const noexcept { return closures == nullptr || !closures->is_closed(e); }
};

class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual std::string name() const = 0;
    virtual std::string time_complexity() const = 0;
    virtual std::string space_complexity() const = 0;

    /// Optimal algorithms must agree on path cost; this flags the ones that
    /// do, so the differential tests know what to assert.
    virtual bool guarantees_optimal() const { return true; }
    /// Whether the algorithm tolerates negative edge weights.
    virtual bool handles_negative_weights() const { return false; }
    /// Whether it needs node coordinates (heuristic search).
    virtual bool needs_coordinates() const { return false; }

    virtual SearchResult run(const Graph& g, NodeId source, NodeId target,
                             const SearchOptions& opts = {}) const = 0;

protected:
    /// Result pre-filled with this algorithm's identity.
    SearchResult make_result() const {
        SearchResult r;
        r.algorithm = name();
        r.time_complexity = time_complexity();
        r.space_complexity = space_complexity();
        return r;
    }
};

/// Name -> factory. Keeps the CLI, the race mode and the benchmark harness
/// from each maintaining their own if/else chain over algorithm names.
class Registry {
public:
    static Registry& instance();

    void add(std::string key, std::unique_ptr<Algorithm> (*factory)());
    std::unique_ptr<Algorithm> create(const std::string& key) const;
    bool contains(const std::string& key) const;
    std::vector<std::string> keys() const;

private:
    std::vector<std::pair<std::string, std::unique_ptr<Algorithm> (*)()>> entries_;
};

/// Reconstruct a path from a parent array, guarding against the broken chains
/// that a malformed graph can produce. Returns empty on failure rather than
/// looping forever, which the original `while (curr != source)` loops could do.
std::vector<NodeId> reconstruct_path(const std::vector<NodeId>& parent, NodeId source,
                                     NodeId target);

/// Sum the weights along a node sequence. Returns -1 if any hop is not a real
/// edge, which is what makes it usable as a test oracle.
double path_cost(const Graph& g, const std::vector<NodeId>& path);

}  // namespace agss

/// Registers an Algorithm subclass under a CLI key at static-init time.
#define AGSS_REGISTER_ALGORITHM(key, Type)                                       \
    namespace {                                                                  \
    std::unique_ptr<::agss::Algorithm> agss_make_##Type() {                      \
        return std::make_unique<Type>();                                         \
    }                                                                            \
    const bool agss_registered_##Type = [] {                                     \
        ::agss::Registry::instance().add(key, &agss_make_##Type);                \
        return true;                                                             \
    }();                                                                         \
    }
