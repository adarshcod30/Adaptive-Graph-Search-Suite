// Emscripten entry points.
//
// The browser build removes the Python bridge entirely rather than porting it:
// no subprocess, no shared trace file on disk, no `map` parameter reaching the
// filesystem. The path-traversal and concurrent-overwrite defects the server
// had are not fixed here so much as made unrepresentable -- each tab owns its
// own engine instance and its own memory.
//
// The boundary is deliberately string-in/string-out (JSON): it keeps the
// JavaScript side free of manual memory management, and the payloads are small
// because traces are deltas rather than snapshots.
#include <emscripten/emscripten.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/alt.hpp"
#include "agss/analysis.hpp"
#include "agss/contraction_hierarchy.hpp"
#include "agss/customizable_ch.hpp"
#include "agss/directions.hpp"
#include "agss/graph_builder.hpp"
#include "agss/isochrone.hpp"
#include "agss/json.hpp"
#include "agss/kdtree.hpp"
#include "agss/kshortest.hpp"
#include "agss/loader.hpp"
#include "agss/time_dependent.hpp"
#include "agss/transit.hpp"

namespace {

/// One loaded graph per page. Kept alive between calls so repeated queries
/// against the same city do not re-parse the CSV.
struct Session {
    agss::Graph graph;
    std::unique_ptr<agss::KdTree> index;
    std::string name;
    bool loaded = false;

    /// Prepared once per graph and reused: the whole point of CH is that
    /// preprocessing amortises across queries.
    agss::ContractionHierarchy ch;
    const agss::Graph* ch_prepared_for = nullptr;

    agss::CustomizableCH cch;
    const agss::Graph* cch_prepared_for = nullptr;
    std::unique_ptr<agss::TimeDependentModel> traffic;

    agss::transit::Network network;
    agss::transit::MultiModal multimodal;
    bool transit_loaded = false;
    bool multimodal_ready = false;
};

Session& session() {
    static Session s;
    return s;
}

/// Hand a heap copy to JavaScript. The caller must free it via agss_free;
/// the JS wrapper in agss.js does that in a finally block.
char* to_js(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out == nullptr) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

char* error_json(const std::string& message) {
    std::ostringstream os;
    os << "{\"ok\":false,\"error\":\"" << agss::json::escape(message) << "\"}";
    return to_js(os.str());
}

const agss::Graph& active_graph() {
    auto& s = session();
    return s.multimodal_ready ? s.multimodal.graph : s.graph;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void agss_free(char* p) {
    std::free(p);
}

/// Parse CSV text already fetched by the browser. Validation is identical to
/// the native path, so a malformed file produces a file:line error here too.
EMSCRIPTEN_KEEPALIVE
char* agss_load_graph(const char* name, const char* nodes_csv, const char* edges_csv,
                      int geographic, int lenient) {
    auto& s = session();
    s.multimodal_ready = false;
    s.ch_prepared_for = nullptr;  // the hierarchy belongs to the old graph

    // load_csv reads paths, so mirror its parsing over in-memory text by
    // writing to the Emscripten in-memory filesystem -- no host disk involved.
    const std::string npath = "/tmp/agss_nodes.csv";
    const std::string epath = "/tmp/agss_edges.csv";
    {
        FILE* fn = std::fopen(npath.c_str(), "w");
        if (fn == nullptr) return error_json("cannot stage nodes in memory");
        std::fputs(nodes_csv, fn);
        std::fclose(fn);
        FILE* fe = std::fopen(epath.c_str(), "w");
        if (fe == nullptr) return error_json("cannot stage edges in memory");
        std::fputs(edges_csv, fe);
        std::fclose(fe);
    }

    agss::LoadOptions opts;
    opts.space = geographic ? agss::CoordSpace::Geographic : agss::CoordSpace::Planar;
    opts.lenient = lenient != 0;

    auto loaded = agss::load_csv(npath, epath, opts);
    if (!loaded) return error_json(loaded.error().what());

    s.graph = std::move(loaded.value().graph);
    s.index = std::make_unique<agss::KdTree>(s.graph);
    s.name = name ? name : "graph";
    s.loaded = true;

    std::ostringstream os;
    os << "{\"ok\":true,\"name\":\"" << agss::json::escape(s.name)
       << "\",\"nodes\":" << s.graph.num_nodes() << ",\"edges\":" << s.graph.num_edges()
       << ",\"geographic\":" << (geographic ? "true" : "false")
       << ",\"admissible\":" << (s.graph.heuristic_is_admissible() ? "true" : "false")
       << ",\"admissibility\":" << agss::json::number(s.graph.heuristic_admissibility())
       << ",\"warnings\":[";
    for (std::size_t i = 0; i < loaded.value().warnings.size() && i < 20; ++i) {
        if (i) os << ",";
        os << "\"" << agss::json::escape(loaded.value().warnings[i]) << "\"";
    }
    os << "],\"warningCount\":" << loaded.value().warnings.size() << "}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_algorithms() {
    std::ostringstream os;
    os << "{\"ok\":true,\"algorithms\":[";
    bool first = true;
    for (const auto& k : agss::Registry::instance().keys()) {
        auto alg = agss::Registry::instance().create(k);
        if (!first) os << ",";
        first = false;
        os << "{\"key\":\"" << k << "\",\"name\":\"" << agss::json::escape(alg->name())
           << "\",\"time\":\"" << agss::json::escape(alg->time_complexity()) << "\",\"space\":\""
           << agss::json::escape(alg->space_complexity())
           << "\",\"optimal\":" << (alg->guarantees_optimal() ? "true" : "false")
           << ",\"needsCoords\":" << (alg->needs_coordinates() ? "true" : "false") << "}";
    }
    os << "]}";
    return to_js(os.str());
}

/// Run one search. `closed` is a comma-separated "u:v" list of shut roads.
EMSCRIPTEN_KEEPALIVE
char* agss_route(const char* alg_key, int source, int target, int want_trace, const char* closed) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();

    auto alg = agss::Registry::instance().create(alg_key ? alg_key : "dijkstra");
    if (!alg) return error_json(std::string("unknown algorithm: ") + (alg_key ? alg_key : ""));
    if (source < 0 || target < 0 || source >= g.num_nodes() || target >= g.num_nodes()) {
        return error_json("source or target is outside the graph");
    }

    agss::ClosureMask mask(g.num_edges());
    bool any_closed = false;
    if (closed != nullptr && *closed != '\0') {
        std::istringstream in(closed);
        std::string tok;
        while (std::getline(in, tok, ',')) {
            const auto colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const int u = std::atoi(tok.substr(0, colon).c_str());
            const int v = std::atoi(tok.substr(colon + 1).c_str());
            if (u >= 0 && v >= 0 && u < g.num_nodes() && v < g.num_nodes()) {
                if (mask.close_road(g, u, v) > 0) any_closed = true;
            }
        }
    }

    agss::Trace trace;
    agss::SearchOptions opts;
    if (want_trace) {
        trace.reserve(static_cast<std::size_t>(g.num_nodes()) * 2);
        opts.trace = &trace;
    }
    if (any_closed) opts.closures = &mask;

    const auto res = alg->run(g, source, target, opts);

    std::ostringstream os;
    agss::json::TraceDocument doc;
    doc.graph = nullptr;  // the client already holds the geometry
    doc.result = &res;
    doc.trace = want_trace ? &trace : nullptr;
    doc.closures = any_closed ? &mask : nullptr;
    agss::json::write_trace(os, doc);
    return to_js(os.str());
}

/// Geometry, sent once per graph so route calls stay small.
/// Preprocess the active graph for Contraction Hierarchies.
///
/// Exposed separately from the query so the page can show the cost honestly:
/// CH trades a one-off build against every later query being nearly free, and
/// hiding the build behind the first search would misrepresent that bargain.
EMSCRIPTEN_KEEPALIVE
char* agss_ch_build(double budget_ms) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();

    agss::ContractionHierarchy::BuildOptions opts;
    if (budget_ms > 0) opts.budget_ms = budget_ms;
    s.ch.build(g, opts);
    s.ch_prepared_for = &g;

    const auto& st = s.ch.stats();
    std::ostringstream os;
    os << "{\"ok\":true,\"buildMs\":" << agss::json::number(st.build_ms)
       << ",\"shortcuts\":" << st.shortcuts << ",\"originalEdges\":" << st.original_edges
       << ",\"edgeGrowth\":" << agss::json::number(st.edge_growth)
       << ",\"witnessSearches\":" << st.witness_searches
       << ",\"aborted\":" << (st.aborted ? "true" : "false") << "}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_ch_ready() {
    auto& s = session();
    const bool ok = s.loaded && s.ch.ready() && s.ch_prepared_for == &active_graph();
    std::ostringstream os;
    os << "{\"ok\":true,\"ready\":" << (ok ? "true" : "false") << "}";
    return to_js(os.str());
}

/// Query the prepared hierarchy. Refuses rather than silently rebuilding, so
/// the page can prompt for the build and show what it cost.
EMSCRIPTEN_KEEPALIVE
char* agss_ch_query(int source, int target, int want_trace) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();
    if (!s.ch.ready() || s.ch_prepared_for != &g) {
        return error_json("this graph has not been preprocessed yet");
    }

    agss::Trace trace;
    agss::SearchOptions opts;
    if (want_trace) {
        trace.reserve(4096);
        opts.trace = &trace;
    }
    const auto res = s.ch.query(source, target, opts);

    std::ostringstream os;
    agss::json::TraceDocument doc;
    doc.result = &res;
    doc.trace = want_trace ? &trace : nullptr;
    agss::json::write_trace(os, doc);
    return to_js(os.str());
}

/// Customizable CH: build the metric-independent structure once, then apply a
/// metric in milliseconds. `hour` < 0 uses the graph's own weights; otherwise
/// it customizes for traffic at that hour of the day.
EMSCRIPTEN_KEEPALIVE
char* agss_cch_customize(double hour) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();

    double build_ms = 0.0;
    if (!s.cch.ready() || s.cch_prepared_for != &g) {
        agss::CustomizableCH::BuildOptions o;
        o.budget_ms = 30000.0;
        s.cch.build(g, o);
        s.cch_prepared_for = &g;
        build_ms = s.cch.stats().build_ms;
    }

    if (hour < 0) {
        s.cch.customize();
    } else {
        if (!s.traffic) s.traffic = std::make_unique<agss::TimeDependentModel>(g);
        std::vector<double> w(static_cast<std::size_t>(g.num_edges()));
        for (agss::EdgeId e = 0; e < g.num_edges(); ++e) {
            w[static_cast<std::size_t>(e)] = s.traffic->travel_time(e, hour * 3600.0);
        }
        s.cch.customize(w);
    }

    const auto& st = s.cch.stats();
    std::ostringstream os;
    os << "{\"ok\":true,\"builtNow\":" << (build_ms > 0 ? "true" : "false")
       << ",\"buildMs\":" << agss::json::number(st.build_ms)
       << ",\"customizeMs\":" << agss::json::number(st.customize_ms)
       << ",\"chordalEdges\":" << st.chordal_edges
       << ",\"edgeGrowth\":" << agss::json::number(st.edge_growth)
       << ",\"hour\":" << agss::json::number(hour) << "}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_cch_query(int source, int target, int want_trace) {
    auto& s = session();
    if (!s.cch.ready()) return error_json("customize the hierarchy first");
    agss::Trace trace;
    agss::SearchOptions opts;
    if (want_trace) {
        trace.reserve(4096);
        opts.trace = &trace;
    }
    const auto res = s.cch.query(source, target, opts);
    std::ostringstream os;
    agss::json::TraceDocument doc;
    doc.result = &res;
    doc.trace = want_trace ? &trace : nullptr;
    agss::json::write_trace(os, doc);
    return to_js(os.str());
}

/// Duration of the same trip departing at each hour of the day.
EMSCRIPTEN_KEEPALIVE
char* agss_traffic_scan(int source, int target, int samples) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();
    if (!s.traffic) s.traffic = std::make_unique<agss::TimeDependentModel>(g);

    const auto scan = agss::scan_departures(*s.traffic, source, target, samples > 0 ? samples : 24);
    std::ostringstream os;
    os << "{\"ok\":true,\"bestDeparture\":" << agss::json::number(scan.best_departure)
       << ",\"bestDuration\":" << agss::json::number(scan.best_duration)
       << ",\"worstDeparture\":" << agss::json::number(scan.worst_departure)
       << ",\"worstDuration\":" << agss::json::number(scan.worst_duration) << ",\"departures\":[";
    for (std::size_t i = 0; i < scan.departures.size(); ++i) {
        if (i) os << ',';
        os << agss::json::number(scan.departures[i]);
    }
    os << "],\"durations\":[";
    for (std::size_t i = 0; i < scan.durations.size(); ++i) {
        if (i) os << ',';
        os << agss::json::number(scan.durations[i]);
    }
    os << "]}";
    return to_js(os.str());
}

/// Geometry as packed binary rather than JSON.
///
/// JSON is fine for a 20k-node city and fatal for a 200k-node country: the
/// text alone runs to tens of megabytes, the ostringstream needs it twice
/// over, and the browser then materialises half a million small objects to
/// parse it. The national highway network failed with "memory access out of
/// bounds" before it drew a single pixel.
///
/// Coordinates go out as interleaved float64 (precision matters -- float32
/// quantises longitude to about a metre) and the edge list as int32 pairs.
/// For 207k nodes that is 3.3 MB and 2.2 MB, read straight into typed arrays
/// with no parsing at all.
EMSCRIPTEN_KEEPALIVE int agss_node_count() {
    auto& s = session();
    return s.loaded ? active_graph().num_nodes() : 0;
}

EMSCRIPTEN_KEEPALIVE int agss_edge_count() {
    auto& s = session();
    return s.loaded ? active_graph().num_edges() : 0;
}

EMSCRIPTEN_KEEPALIVE int agss_is_geographic() {
    auto& s = session();
    return s.loaded && active_graph().coord_space() == agss::CoordSpace::Geographic ? 1 : 0;
}

/// 2 * node_count doubles, interleaved (x, y). Caller frees with agss_free.
EMSCRIPTEN_KEEPALIVE double* agss_coords_buffer() {
    auto& s = session();
    if (!s.loaded) return nullptr;
    const auto& g = active_graph();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    auto* out = static_cast<double*>(std::malloc(n * 2 * sizeof(double)));
    if (out == nullptr) return nullptr;
    for (std::size_t i = 0; i < n; ++i) {
        out[i * 2] = g.x(static_cast<agss::NodeId>(i));
        out[i * 2 + 1] = g.y(static_cast<agss::NodeId>(i));
    }
    return out;
}

/// 2 * edge_count int32s, interleaved (u, v). Caller frees with agss_free.
EMSCRIPTEN_KEEPALIVE int* agss_edges_buffer() {
    auto& s = session();
    if (!s.loaded) return nullptr;
    const auto& g = active_graph();
    auto* out =
        static_cast<int*>(std::malloc(static_cast<std::size_t>(g.num_edges()) * 2 * sizeof(int)));
    if (out == nullptr) return nullptr;
    std::size_t k = 0;
    for (agss::NodeId u = 0; u < g.num_nodes(); ++u) {
        for (agss::EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            out[k++] = u;
            out[k++] = g.edge_target(e);
        }
    }
    return out;
}

EMSCRIPTEN_KEEPALIVE
char* agss_graph_json() {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    std::ostringstream os;
    agss::json::write_graph(os, active_graph(), 0);
    return to_js(os.str());
}

/// Run one algorithm `reps` times and report the total plus the median.
///
/// Browsers clamp performance.now() to ~100 microseconds as a Spectre
/// mitigation, and steady_clock rides on it, so a single search over a small
/// graph reports 0.0 or 0.1 ms and nothing in between. Timing a batch and
/// dividing amortises the clamp away, which is the only way to get a
/// meaningful per-query figure in the browser.
EMSCRIPTEN_KEEPALIVE
char* agss_bench(const char* alg_key, int source, int target, int reps) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();
    auto alg = agss::Registry::instance().create(alg_key ? alg_key : "dijkstra");
    if (!alg) return error_json("unknown algorithm");
    if (reps < 1) reps = 1;
    if (reps > 2000) reps = 2000;

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    agss::SearchResult last;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        last = alg->run(g, source, target, {});  // no Trace: zero observer cost
        times.push_back(last.algorithm_ms);
    }
    const double wall =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::sort(times.begin(), times.end());

    std::ostringstream os;
    os << "{\"ok\":true,\"reps\":" << reps << ",\"amortisedMs\":" << agss::json::number(wall / reps)
       << ",\"medianMs\":" << agss::json::number(times[times.size() / 2])
       << ",\"totalMs\":" << agss::json::number(wall) << ",\"expanded\":" << last.nodes_expanded
       << ",\"relaxed\":" << last.edges_relaxed
       << ",\"success\":" << (last.success ? "true" : "false")
       << ",\"cost\":" << agss::json::number(last.path_cost) << "}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_race(const char* keys_csv, int source, int target) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();
    if (source < 0 || target < 0 || source >= g.num_nodes() || target >= g.num_nodes()) {
        return error_json("source or target is outside the graph");
    }

    std::vector<std::string> keys;
    if (keys_csv != nullptr && *keys_csv != '\0') {
        std::istringstream in(keys_csv);
        std::string tok;
        while (std::getline(in, tok, ',')) keys.push_back(tok);
    } else {
        keys = agss::Registry::instance().keys();
    }

    struct Row {
        std::string key;
        agss::SearchResult res;
        bool claims_optimal;
    };
    std::vector<Row> rows;
    double best = -1.0;
    for (const auto& k : keys) {
        auto alg = agss::Registry::instance().create(k);
        if (!alg) continue;
        auto res = alg->run(g, source, target, {});
        if (res.success && alg->guarantees_optimal() && (best < 0.0 || res.path_cost < best)) {
            best = res.path_cost;
        }
        rows.push_back({k, std::move(res), alg->guarantees_optimal()});
    }

    std::ostringstream os;
    os << "{\"ok\":true,\"referenceCost\":" << agss::json::number(best) << ",\"rows\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (i) os << ",";
        const bool declined = r.res.algorithm.find("skipped") != std::string::npos;
        os << "{\"key\":\"" << r.key << "\",\"name\":\"" << agss::json::escape(r.res.algorithm)
           << "\",\"ms\":" << agss::json::number(r.res.algorithm_ms)
           << ",\"expanded\":" << r.res.nodes_expanded << ",\"relaxed\":" << r.res.edges_relaxed
           << ",\"success\":" << (r.res.success ? "true" : "false")
           << ",\"declined\":" << (declined ? "true" : "false")
           << ",\"hops\":" << (r.res.path.empty() ? 0 : r.res.path.size() - 1)
           << ",\"cost\":" << agss::json::number(r.res.path_cost)
           << ",\"claimsOptimal\":" << (r.claims_optimal ? "true" : "false") << ",\"path\":[";
        for (std::size_t j = 0; j < r.res.path.size(); ++j) {
            if (j) os << ",";
            os << r.res.path[j];
        }
        os << "]}";
    }
    os << "]}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_analyze(const char* what, int source, int target, int samples) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();
    const std::string w = what ? what : "bridges";
    std::ostringstream os;

    if (w == "bridges") {
        auto rep = agss::analysis::find_bridges(g);
        std::sort(rep.bridges.begin(), rep.bridges.end(),
                  [](const auto& a, const auto& b) { return a.isolated_nodes > b.isolated_nodes; });
        os << "{\"ok\":true,\"kind\":\"bridges\",\"ms\":" << agss::json::number(rep.elapsed_ms)
           << ",\"components\":" << rep.component_count << ",\"bridgeCount\":" << rep.bridges.size()
           << ",\"articulationCount\":" << rep.articulation_points.size() << ",\"bridges\":[";
        for (std::size_t i = 0; i < rep.bridges.size(); ++i) {
            if (i) os << ",";
            os << "{\"u\":" << rep.bridges[i].u << ",\"v\":" << rep.bridges[i].v
               << ",\"isolates\":" << rep.bridges[i].isolated_nodes << "}";
        }
        os << "],\"articulation\":[";
        for (std::size_t i = 0; i < rep.articulation_points.size(); ++i) {
            if (i) os << ",";
            os << rep.articulation_points[i];
        }
        os << "]}";
    } else if (w == "centrality") {
        const auto rep = agss::analysis::betweenness_centrality(g, samples);
        os << "{\"ok\":true,\"kind\":\"centrality\",\"ms\":" << agss::json::number(rep.elapsed_ms)
           << ",\"exact\":" << (rep.exact ? "true" : "false")
           << ",\"sampled\":" << rep.sources_sampled << ",\"scores\":[";
        for (std::size_t i = 0; i < rep.betweenness.size(); ++i) {
            if (i) os << ",";
            os << agss::json::number(rep.betweenness[i]);
        }
        os << "]}";
    } else if (w == "scc") {
        const auto rep = agss::analysis::strongly_connected_components(g);
        os << "{\"ok\":true,\"kind\":\"scc\",\"ms\":" << agss::json::number(rep.elapsed_ms)
           << ",\"count\":" << rep.count << ",\"componentOf\":[";
        for (std::size_t i = 0; i < rep.component_of.size(); ++i) {
            if (i) os << ",";
            os << rep.component_of[i];
        }
        os << "]}";
    } else if (w == "mst") {
        const auto rep = agss::analysis::minimum_spanning_tree(g);
        os << "{\"ok\":true,\"kind\":\"mst\",\"ms\":" << agss::json::number(rep.elapsed_ms)
           << ",\"totalWeight\":" << agss::json::number(rep.total_weight)
           << ",\"spans\":" << (rep.spans_all_nodes ? "true" : "false") << ",\"edges\":[";
        for (std::size_t i = 0; i < rep.edges.size(); ++i) {
            if (i) os << ",";
            os << "[" << rep.edges[i].first << "," << rep.edges[i].second << "]";
        }
        os << "]}";
    } else if (w == "flow") {
        const auto rep = agss::analysis::max_flow(g, source, target);
        os << "{\"ok\":true,\"kind\":\"flow\",\"ms\":" << agss::json::number(rep.elapsed_ms)
           << ",\"maxFlow\":" << agss::json::number(rep.max_flow) << ",\"minCut\":[";
        for (std::size_t i = 0; i < rep.min_cut.size(); ++i) {
            if (i) os << ",";
            os << "[" << rep.min_cut[i].first << "," << rep.min_cut[i].second << "]";
        }
        os << "]}";
    } else {
        return error_json("unknown analysis: " + w);
    }
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_isochrone(int origin, const char* cutoffs_csv) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();

    std::vector<double> cutoffs;
    if (cutoffs_csv != nullptr) {
        std::istringstream in(cutoffs_csv);
        std::string tok;
        while (std::getline(in, tok, ',')) {
            const double v = std::atof(tok.c_str());
            if (v > 0.0) cutoffs.push_back(v);
        }
    }
    if (cutoffs.empty()) return error_json("no valid cutoffs given");

    const auto rep = agss::isochrone(g, origin, cutoffs);
    std::ostringstream os;
    os << "{\"ok\":true,\"ms\":" << agss::json::number(rep.elapsed_ms) << ",\"bands\":[";
    for (std::size_t i = 0; i < rep.bands.size(); ++i) {
        const auto& b = rep.bands[i];
        if (i) os << ",";
        os << "{\"cutoff\":" << agss::json::number(b.cutoff) << ",\"count\":" << b.nodes.size()
           << ",\"nodes\":[";
        for (std::size_t j = 0; j < b.nodes.size(); ++j) {
            if (j) os << ",";
            os << b.nodes[j];
        }
        os << "],\"hull\":[";
        for (std::size_t j = 0; j < b.hull.size(); ++j) {
            if (j) os << ",";
            os << "[" << agss::json::number(b.hull[j].first) << ","
               << agss::json::number(b.hull[j].second) << "]";
        }
        os << "]}";
    }
    os << "]}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_kpaths(int source, int target, int k) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto rep = agss::k_shortest_paths(active_graph(), source, target, k);
    std::ostringstream os;
    os << "{\"ok\":true,\"ms\":" << agss::json::number(rep.elapsed_ms) << ",\"routes\":[";
    for (std::size_t i = 0; i < rep.routes.size(); ++i) {
        if (i) os << ",";
        os << "{\"cost\":" << agss::json::number(rep.routes[i].cost) << ",\"path\":[";
        for (std::size_t j = 0; j < rep.routes[i].path.size(); ++j) {
            if (j) os << ",";
            os << rep.routes[i].path[j];
        }
        os << "]}";
    }
    os << "]}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_directions(const char* path_csv) {
    auto& s = session();
    if (!s.loaded) return error_json("no graph loaded");
    const auto& g = active_graph();

    std::vector<agss::NodeId> path;
    if (path_csv != nullptr) {
        std::istringstream in(path_csv);
        std::string tok;
        while (std::getline(in, tok, ',')) {
            if (!tok.empty()) path.push_back(std::atoi(tok.c_str()));
        }
    }
    const auto dir = agss::build_directions(g, path);
    std::ostringstream os;
    os << "{\"ok\":true,\"totalDistance\":" << agss::json::number(dir.total_distance)
       << ",\"steps\":[";
    for (std::size_t i = 0; i < dir.steps.size(); ++i) {
        const auto& st = dir.steps[i];
        if (i) os << ",";
        os << "{\"at\":" << st.at << ",\"maneuver\":\"" << agss::to_string(st.maneuver)
           << "\",\"distance\":" << agss::json::number(st.distance)
           << ",\"bearing\":" << agss::json::number(st.bearing)
           << ",\"turn\":" << agss::json::number(st.turn) << ",\"text\":\""
           << agss::json::escape(st.text.empty() ? agss::to_string(st.maneuver) : st.text) << "\"}";
    }
    os << "]}";
    return to_js(os.str());
}

/// Snap a coordinate to the nearest node. What makes clicking a real city map
/// usable: O(log V) instead of a linear scan of 48k nodes per click.
EMSCRIPTEN_KEEPALIVE
char* agss_nearest(double x, double y) {
    auto& s = session();
    if (!s.loaded || !s.index) return error_json("no graph loaded");
    if (s.multimodal_ready) return error_json("nearest is unavailable on a multi-modal graph");
    const auto n = s.index->nearest(x, y);
    if (n == agss::kInvalidNode) return error_json("graph is empty");
    std::ostringstream os;
    os << "{\"ok\":true,\"node\":" << n << ",\"x\":" << agss::json::number(s.graph.x(n))
       << ",\"y\":" << agss::json::number(s.graph.y(n))
       << ",\"externalId\":" << s.graph.external_id(n) << "}";
    return to_js(os.str());
}

EMSCRIPTEN_KEEPALIVE
char* agss_load_transit(const char* stations_csv, const char* links_csv) {
    auto& s = session();
    const std::string dir = "/tmp/agss_transit";
    mkdir(dir.c_str(), 0777);
    {
        FILE* fs = std::fopen((dir + "/stations.csv").c_str(), "w");
        if (fs == nullptr) return error_json("cannot stage stations in memory");
        std::fputs(stations_csv, fs);
        std::fclose(fs);
        FILE* fl = std::fopen((dir + "/links.csv").c_str(), "w");
        if (fl == nullptr) return error_json("cannot stage links in memory");
        std::fputs(links_csv, fl);
        std::fclose(fl);
    }
    auto net = agss::transit::load(dir);
    if (!net) return error_json(net.error().what());
    s.network = std::move(net.value());
    s.transit_loaded = true;

    std::ostringstream os;
    os << "{\"ok\":true,\"stations\":" << s.network.station_count()
       << ",\"links\":" << s.network.link_count() << ",\"systems\":[";
    for (std::size_t i = 0; i < s.network.systems.size(); ++i) {
        if (i) os << ",";
        os << "\"" << agss::json::escape(s.network.systems[i]) << "\"";
    }
    os << "]}";
    return to_js(os.str());
}

/// Build a rail-only or road+rail graph and make it the active one.
EMSCRIPTEN_KEEPALIVE
char* agss_build_transit(const char* city, int with_roads) {
    auto& s = session();
    if (!s.transit_loaded) return error_json("no transit data loaded");

    auto net =
        (city != nullptr && *city != '\0') ? agss::transit::filter(s.network, city) : s.network;
    if (net.stations.empty()) return error_json("no stations matched that city or system");

    agss::transit::BuildOptions opts;
    auto built = with_roads && s.loaded ? agss::transit::combine(s.graph, net, opts)
                                        : agss::transit::rail_only(net, opts);
    if (!built) return error_json(built.error().what());

    s.multimodal = std::move(built.value());
    s.multimodal_ready = true;
    s.loaded = true;

    std::ostringstream os;
    os << "{\"ok\":true,\"roadNodes\":" << s.multimodal.layers.road_node_count
       << ",\"stationNodes\":" << s.multimodal.layers.station_node_count
       << ",\"edges\":" << s.multimodal.graph.num_edges() << ",\"stations\":[";
    bool first = true;
    for (const auto& st : net.stations) {
        auto it = s.multimodal.layers.station_node.find(st.id);
        if (it == s.multimodal.layers.station_node.end()) continue;
        if (!first) os << ",";
        first = false;
        os << "{\"node\":" << it->second << ",\"name\":\"" << agss::json::escape(st.name)
           << "\",\"system\":\"" << agss::json::escape(st.system)
           << "\",\"lat\":" << agss::json::number(st.lat)
           << ",\"lon\":" << agss::json::number(st.lon) << "}";
    }
    os << "],\"warnings\":[";
    for (std::size_t i = 0; i < s.multimodal.warnings.size() && i < 10; ++i) {
        if (i) os << ",";
        os << "\"" << agss::json::escape(s.multimodal.warnings[i]) << "\"";
    }
    os << "]}";
    return to_js(os.str());
}

/// Drop back to the plain road graph after a transit build.
EMSCRIPTEN_KEEPALIVE
char* agss_use_road_graph() {
    auto& s = session();
    s.multimodal_ready = false;
    s.ch_prepared_for = nullptr;  // the hierarchy belongs to the old graph
    std::ostringstream os;
    os << "{\"ok\":true,\"nodes\":" << s.graph.num_nodes() << ",\"edges\":" << s.graph.num_edges()
       << "}";
    return to_js(os.str());
}

}  // extern "C"
