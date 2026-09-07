#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/analysis.hpp"
#include "agss/directions.hpp"
#include "agss/isochrone.hpp"
#include "agss/json.hpp"
#include "agss/kdtree.hpp"
#include "agss/kshortest.hpp"
#include "agss/loader.hpp"
#include "agss/transit.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
    std::string command;
    std::map<std::string, std::string> opts;
    std::vector<std::string> positional;

    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string str(const std::string& k, const std::string& def = {}) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
    long num(const std::string& k, long def) const {
        auto it = opts.find(k);
        if (it == opts.end()) return def;
        try {
            return std::stol(it->second);
        } catch (...) {
            return def;
        }
    }
    double real(const std::string& k, double def) const {
        auto it = opts.find(k);
        if (it == opts.end()) return def;
        try {
            return std::stod(it->second);
        } catch (...) {
            return def;
        }
    }
};

Args parse_args(int argc, char** argv) {
    Args a;
    int i = 1;
    if (i < argc && argv[i][0] != '-') a.command = argv[i++];
    for (; i < argc; ++i) {
        std::string t = argv[i];
        if (t.rfind("--", 0) == 0) {
            const auto eq = t.find('=');
            if (eq != std::string::npos) {
                a.opts[t.substr(2, eq - 2)] = t.substr(eq + 1);
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                a.opts[t.substr(2)] = argv[++i];
            } else {
                a.opts[t.substr(2)] = "true";
            }
        } else {
            a.positional.push_back(t);
        }
    }
    return a;
}

void print_usage() {
    std::cout << R"(agss - Adaptive Graph Search Suite

USAGE
  agss <command> [options]

COMMANDS
  route       Run one algorithm and optionally emit a trace
  race        Run several algorithms on the same query and compare them
  bench       Time every algorithm across maps, with tracing disabled
  verify      Differential correctness check across optimal algorithms
  analyze     Bridges, articulation points, SCC, MST, centrality, max-flow
  isochrone   Reachability bands from an origin
  kpaths      K shortest loopless routes (Yen)
  transit     Inspect or route over metro networks, optionally multi-modal
  maps        List available maps
  algorithms  List registered algorithms

COMMON OPTIONS
  --graph <dir>        Directory holding nodes.csv and edges.csv
  --geo                Treat coordinates as lat/lon (great-circle metric)
  --source <id>        Source node (external id)
  --target <id>        Target node (external id)
  --lenient            Skip malformed CSV rows instead of failing
  --trace-out <file>   Write a JSON trace
  --quiet              Machine-readable output only

EXAMPLES
  agss route --graph data/maps/Delhi_NCR --alg astar --source 0 --target 150
  agss race  --graph data/maps/Delhi_NCR --source 0 --target 150
  agss verify --graph data/maps/Delhi_NCR --samples 200
  agss analyze --graph data/maps/Bengaluru_Traffic --what bridges
  agss transit --transit data/transit --list
)";
}

agss::Result<agss::LoadReport> load_graph(const Args& a) {
    agss::LoadOptions o;
    o.space = a.has("geo") ? agss::CoordSpace::Geographic : agss::CoordSpace::Planar;
    o.lenient = a.has("lenient");
    o.allow_negative_weights = a.has("allow-negative");
    return agss::load_csv_dir(a.str("graph"), o);
}

int fail(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    return 1;
}

/// Map an external id from the command line onto a dense node id.
agss::NodeId resolve(const agss::Graph& g, const std::string& raw, bool& ok) {
    ok = false;
    try {
        const auto ext = static_cast<std::int64_t>(std::stoll(raw));
        const agss::NodeId n = g.lookup(ext);
        if (n != agss::kInvalidNode) {
            ok = true;
            return n;
        }
    } catch (...) {
    }
    return agss::kInvalidNode;
}

std::string fmt(double v, int prec = 4) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

// ---------------------------------------------------------------- commands

int cmd_algorithms() {
    auto& reg = agss::Registry::instance();
    std::cout << std::left << std::setw(16) << "KEY" << std::setw(34) << "NAME"
              << std::setw(22) << "TIME" << "OPTIMAL\n";
    for (const auto& k : reg.keys()) {
        auto alg = reg.create(k);
        std::cout << std::left << std::setw(16) << k << std::setw(34) << alg->name()
                  << std::setw(22) << alg->time_complexity()
                  << (alg->guarantees_optimal() ? "yes" : "no") << "\n";
    }
    return 0;
}

int cmd_route(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;
    for (const auto& w : loaded.value().warnings) std::cerr << "warning: " << w << "\n";

    const std::string key = a.str("alg", "dijkstra");
    auto alg = agss::Registry::instance().create(key);
    if (!alg) return fail("unknown algorithm '" + key + "' (try: agss algorithms)");

    bool ok_s = false, ok_t = false;
    const auto s = resolve(g, a.str("source", "0"), ok_s);
    const auto t = resolve(g, a.str("target", "1"), ok_t);
    if (!ok_s) return fail("source node not present in graph");
    if (!ok_t) return fail("target node not present in graph");

    agss::ClosureMask closures(g.num_edges());
    bool any_closed = false;
    if (a.has("close")) {
        // --close "u:v,u2:v2" shuts those roads in both directions.
        std::stringstream ss(a.str("close"));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            const auto colon = tok.find(':');
            if (colon == std::string::npos) continue;
            bool o1 = false, o2 = false;
            const auto u = resolve(g, tok.substr(0, colon), o1);
            const auto v = resolve(g, tok.substr(colon + 1), o2);
            if (o1 && o2) {
                const auto n = closures.close_road(g, u, v);
                if (n > 0) any_closed = true;
                std::cerr << "closed " << n << " directed edge(s) between " << tok << "\n";
            }
        }
    }

    agss::Trace trace;
    agss::SearchOptions opts;
    if (a.has("trace-out")) {
        trace.reserve(static_cast<std::size_t>(g.num_nodes()) * 2);
        opts.trace = &trace;
    }
    if (any_closed) opts.closures = &closures;

    const auto res = alg->run(g, s, t, opts);

    if (!a.has("quiet")) {
        std::cout << "algorithm      : " << res.algorithm << "\n"
                  << "graph          : " << g.num_nodes() << " nodes, " << g.num_edges()
                  << " edges\n"
                  << "success        : " << (res.success ? "yes" : "no") << "\n"
                  << "algorithm time : " << fmt(res.algorithm_ms) << " ms\n"
                  << "nodes expanded : " << res.nodes_expanded << "\n"
                  << "edges relaxed  : " << res.edges_relaxed << "\n";
        if (res.success) {
            std::cout << "path hops      : " << res.path.size() - 1 << "\n"
                      << "path cost      : " << fmt(res.path_cost, 3) << "\n";
        }
    }

    if (res.success && a.has("directions")) {
        const auto dir = agss::build_directions(g, res.path);
        std::cout << "\nDIRECTIONS (" << dir.steps.size() << " steps, total "
                  << fmt(dir.total_distance, 1) << ")\n";
        int n = 1;
        for (const auto& st : dir.steps) {
            std::cout << "  " << std::setw(2) << n++ << ". "
                      << (st.text.empty() ? agss::to_string(st.maneuver) : st.text)
                      << "  [node " << g.external_id(st.at) << "]\n";
        }
    }

    if (a.has("trace-out")) {
        const auto t0 = Clock::now();
        std::ofstream out(a.str("trace-out"));
        if (!out) return fail("cannot write trace to " + a.str("trace-out"));
        agss::json::TraceDocument doc;
        doc.graph = &g;
        doc.result = &res;
        doc.trace = &trace;
        doc.closures = any_closed ? &closures : nullptr;
        doc.trace_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        agss::json::write_trace(out, doc);
        if (!a.has("quiet")) {
            std::cout << "trace          : " << trace.size() << " events -> "
                      << a.str("trace-out") << "\n";
        }
    }
    return res.success ? 0 : 2;
}

int cmd_race(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;

    bool ok_s = false, ok_t = false;
    const auto s = resolve(g, a.str("source", "0"), ok_s);
    const auto t = resolve(g, a.str("target", "1"), ok_t);
    if (!ok_s || !ok_t) return fail("source or target not present in graph");

    std::vector<std::string> keys;
    if (a.has("algs")) {
        std::stringstream ss(a.str("algs"));
        std::string tok;
        while (std::getline(ss, tok, ',')) keys.push_back(tok);
    } else {
        keys = agss::Registry::instance().keys();
    }

    std::cout << std::left << std::setw(14) << "ALG" << std::right << std::setw(10) << "MS"
              << std::setw(11) << "EXPANDED" << std::setw(11) << "RELAXED" << std::setw(7)
              << "HOPS" << std::setw(14) << "COST" << "  OPTIMAL\n";
    std::cout << std::string(76, '-') << "\n";

    double best_cost = -1.0;
    struct Row {
        std::string key;
        agss::SearchResult res;
        bool optimal_claim;
    };
    std::vector<Row> rows;
    for (const auto& k : keys) {
        auto alg = agss::Registry::instance().create(k);
        if (!alg) continue;
        const auto res = alg->run(g, s, t, {});
        if (res.success && alg->guarantees_optimal()) {
            if (best_cost < 0.0 || res.path_cost < best_cost) best_cost = res.path_cost;
        }
        rows.push_back({k, res, alg->guarantees_optimal()});
    }

    for (const auto& r : rows) {
        std::cout << std::left << std::setw(14) << r.key << std::right << std::setw(10)
                  << fmt(r.res.algorithm_ms) << std::setw(11) << r.res.nodes_expanded
                  << std::setw(11) << r.res.edges_relaxed;
        if (r.res.success) {
            const bool is_best = best_cost >= 0.0 && std::abs(r.res.path_cost - best_cost) < 1e-6;
            std::cout << std::setw(7) << r.res.path.size() - 1 << std::setw(14)
                      << fmt(r.res.path_cost, 3) << "  " << (is_best ? "optimal" : "suboptimal")
                      << "\n";
        } else {
            // An algorithm that declined (wrong weight type, graph too large)
            // is not the same as one that searched and found nothing.
            const bool declined = r.res.algorithm.find("skipped") != std::string::npos;
            std::cout << std::setw(7) << "-" << std::setw(14) << "-" << "  "
                      << (declined ? "declined" : "no path") << "\n";
        }
    }
    if (best_cost >= 0.0) std::cout << "\nreference optimal cost: " << fmt(best_cost, 3) << "\n";
    for (const auto& r : rows) {
        const auto pos = r.res.algorithm.find("(skipped");
        if (pos != std::string::npos) {
            std::cout << "  " << r.key << " " << r.res.algorithm.substr(pos) << "\n";
        }
    }
    return 0;
}

int cmd_verify(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;
    if (g.num_nodes() < 2) return fail("graph too small to verify");

    const long samples = a.num("samples", 50);
    // Optimal-by-construction algorithms must all agree; that mutual agreement
    // is the oracle, so no golden files are needed.
    std::vector<std::string> optimal;
    for (const auto& k : agss::Registry::instance().keys()) {
        auto alg = agss::Registry::instance().create(k);
        if (alg->guarantees_optimal() && !alg->handles_negative_weights()) optimal.push_back(k);
        else if (k == "bellmanford" || k == "johnson") optimal.push_back(k);
    }
    if (a.has("skip")) {
        std::stringstream ss(a.str("skip"));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            optimal.erase(std::remove(optimal.begin(), optimal.end(), tok), optimal.end());
        }
    }

    const double adm = g.heuristic_admissibility();
    std::cout << "verifying " << optimal.size() << " optimal algorithms over " << samples
              << " random queries on " << g.num_nodes() << " nodes\n";
    if (!g.heuristic_is_admissible()) {
        // Without this, A* legitimately disagrees with Dijkstra and the run
        // looks like an algorithm bug rather than a data problem.
        std::cout << "  WARNING: an edge weighs only " << fmt(adm, 4)
                  << "x its straight-line length, so the A*/Greedy heuristic "
                     "over-estimates and A* is not optimal on this graph\n";
    }
    for (const auto& k : optimal) std::cout << "  - " << k << "\n";

    std::mt19937_64 rng(static_cast<std::uint64_t>(a.num("seed", 12345)));
    std::uniform_int_distribution<agss::NodeId> pick(0, g.num_nodes() - 1);

    long checked = 0, mismatches = 0, invalid = 0;
    for (long i = 0; i < samples; ++i) {
        const auto s = pick(rng);
        const auto t = pick(rng);
        double ref = -1.0;
        std::string ref_alg;
        for (const auto& k : optimal) {
            auto alg = agss::Registry::instance().create(k);
            const auto res = alg->run(g, s, t, {});
            if (!res.success) continue;

            const double real_cost = agss::path_cost(g, res.path);
            if (real_cost < 0.0) {
                std::cout << "  INVALID  " << k << " s=" << s << " t=" << t
                          << " returned a path containing a non-edge\n";
                ++invalid;
                continue;
            }
            if (res.path.front() != s || res.path.back() != t) {
                std::cout << "  INVALID  " << k << " s=" << s << " t=" << t
                          << " path endpoints do not match the query\n";
                ++invalid;
                continue;
            }
            if (ref < 0.0) {
                ref = real_cost;
                ref_alg = k;
            } else if (std::abs(real_cost - ref) > 1e-6 * std::max(1.0, std::abs(ref))) {
                std::cout << "  MISMATCH s=" << s << " t=" << t << "  " << ref_alg << "="
                          << fmt(ref, 6) << "  " << k << "=" << fmt(real_cost, 6) << "\n";
                ++mismatches;
            }
            ++checked;
        }
    }
    std::cout << "\nchecked " << checked << " results, " << mismatches << " cost mismatches, "
              << invalid << " invalid paths\n";
    return (mismatches == 0 && invalid == 0) ? 0 : 3;
}

int cmd_bench(const Args& a) {
    std::vector<std::string> dirs;
    if (a.has("graph")) {
        dirs.push_back(a.str("graph"));
    } else {
        for (const auto& d : {"Small_Campus", "Mumbai_Pune_Expy", "Indian_Grid", "Delhi_NCR",
                              "Bengaluru_Traffic"}) {
            dirs.push_back(std::string("data/maps/") + d);
        }
    }
    const long reps = a.num("reps", 5);
    std::vector<std::string> keys;
    if (a.has("algs")) {
        std::stringstream ss(a.str("algs"));
        std::string tok;
        while (std::getline(ss, tok, ',')) keys.push_back(tok);
    } else {
        keys = agss::Registry::instance().keys();
    }

    const bool md = a.has("markdown");
    if (md) {
        std::cout << "| map | nodes | edges | algorithm | median ms | expanded | cost |\n";
        std::cout << "|---|---|---|---|---|---|---|\n";
    }

    for (const auto& dir : dirs) {
        agss::LoadOptions o;
        o.space = a.has("geo") ? agss::CoordSpace::Geographic : agss::CoordSpace::Planar;
        o.lenient = true;
        auto loaded = agss::load_csv_dir(dir, o);
        if (!loaded) {
            std::cerr << "skipping " << dir << ": " << loaded.error().what() << "\n";
            continue;
        }
        const auto& g = loaded.value().graph;
        const auto s = agss::NodeId{0};
        const auto t = static_cast<agss::NodeId>(g.num_nodes() - 1);
        const auto name = dir.substr(dir.find_last_of('/') + 1);

        if (!md) {
            std::cout << "\n" << name << "  (" << g.num_nodes() << " nodes, " << g.num_edges()
                      << " edges)\n"
                      << std::left << std::setw(16) << "ALG" << std::right << std::setw(12)
                      << "MEDIAN ms" << std::setw(12) << "EXPANDED" << std::setw(14) << "COST"
                      << "\n"
                      << std::string(54, '-') << "\n";
        }
        for (const auto& k : keys) {
            auto alg = agss::Registry::instance().create(k);
            if (!alg) continue;
            std::vector<double> times;
            agss::SearchResult last;
            for (long r = 0; r < reps; ++r) {
                // No Trace is passed, so this measures the algorithm with zero
                // observer overhead.
                last = alg->run(g, s, t, {});
                times.push_back(last.algorithm_ms);
            }
            std::sort(times.begin(), times.end());
            const double median = times[times.size() / 2];
            if (md) {
                std::cout << "| " << name << " | " << g.num_nodes() << " | " << g.num_edges()
                          << " | " << k << " | " << fmt(median) << " | " << last.nodes_expanded
                          << " | " << (last.success ? fmt(last.path_cost, 2) : "-") << " |\n";
            } else {
                std::cout << std::left << std::setw(16) << k << std::right << std::setw(12)
                          << fmt(median) << std::setw(12) << last.nodes_expanded << std::setw(14)
                          << (last.success ? fmt(last.path_cost, 2) : "-") << "\n";
            }
        }
    }
    return 0;
}

int cmd_analyze(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;
    const std::string what = a.str("what", "all");
    const bool all = what == "all";

    if (all || what == "bridges") {
        const auto rep = agss::analysis::find_bridges(g);
        std::cout << "\nBRIDGES & ARTICULATION POINTS  (" << fmt(rep.elapsed_ms) << " ms)\n"
                  << "  connected components : " << rep.component_count << "\n"
                  << "  bridges              : " << rep.bridges.size() << "\n"
                  << "  articulation points  : " << rep.articulation_points.size() << "\n";
        auto top = rep.bridges;
        std::sort(top.begin(), top.end(), [](const auto& x, const auto& y) {
            return x.isolated_nodes > y.isolated_nodes;
        });
        const std::size_t show = std::min<std::size_t>(10, top.size());
        if (show) std::cout << "  most critical roads (by nodes cut off):\n";
        for (std::size_t i = 0; i < show; ++i) {
            std::cout << "    " << g.external_id(top[i].u) << " -- " << g.external_id(top[i].v)
                      << "   isolates " << top[i].isolated_nodes << " nodes\n";
        }
    }
    if (all || what == "scc") {
        const auto rep = agss::analysis::strongly_connected_components(g);
        auto sizes = rep.sizes;
        std::sort(sizes.rbegin(), sizes.rend());
        std::cout << "\nSTRONGLY CONNECTED COMPONENTS  (" << fmt(rep.elapsed_ms) << " ms)\n"
                  << "  components : " << rep.count << "\n"
                  << "  largest    : " << (sizes.empty() ? 0 : sizes[0]) << " nodes\n";
        if (rep.count > 1) {
            std::cout << "  note: more than one component means some areas cannot be reached "
                         "from others by following edge direction\n";
        }
    }
    if (all || what == "mst") {
        const auto rep = agss::analysis::minimum_spanning_tree(g);
        std::cout << "\nMINIMUM SPANNING TREE  (" << fmt(rep.elapsed_ms) << " ms)\n"
                  << "  edges chosen : " << rep.edges.size() << "\n"
                  << "  total weight : " << fmt(rep.total_weight, 2) << "\n"
                  << "  spans graph  : " << (rep.spans_all_nodes ? "yes" : "no") << "\n";
    }
    if (all || what == "centrality") {
        const long sample = a.num("samples", g.num_nodes() > 2000 ? 256 : 0);
        const auto rep = agss::analysis::betweenness_centrality(g, sample);
        std::vector<agss::NodeId> order(static_cast<std::size_t>(g.num_nodes()));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](agss::NodeId x, agss::NodeId y) {
            return rep.betweenness[x] > rep.betweenness[y];
        });
        std::cout << "\nBETWEENNESS CENTRALITY  (" << fmt(rep.elapsed_ms) << " ms, "
                  << (rep.exact ? "exact" : "sampled from " +
                                                std::to_string(rep.sources_sampled) + " sources")
                  << ")\n  busiest intersections:\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(10, order.size()); ++i) {
            std::cout << "    node " << g.external_id(order[i]) << "   score "
                      << fmt(rep.betweenness[order[i]], 1) << "\n";
        }
    }
    if (what == "flow") {
        bool o1 = false, o2 = false;
        const auto s = resolve(g, a.str("source", "0"), o1);
        const auto t = resolve(g, a.str("target", "1"), o2);
        if (!o1 || !o2) return fail("flow needs valid --source and --target");
        const auto rep = agss::analysis::max_flow(g, s, t);
        std::cout << "\nMAX FLOW / MIN CUT  (" << fmt(rep.elapsed_ms) << " ms)\n"
                  << "  max flow      : " << fmt(rep.max_flow, 3) << "\n"
                  << "  min cut edges : " << rep.min_cut.size() << "\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(10, rep.min_cut.size()); ++i) {
            std::cout << "    " << g.external_id(rep.min_cut[i].first) << " -> "
                      << g.external_id(rep.min_cut[i].second) << "\n";
        }
    }
    return 0;
}

int cmd_isochrone(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;

    bool ok = false;
    const auto origin = resolve(g, a.str("source", "0"), ok);
    if (!ok) return fail("origin node not present in graph");

    std::vector<double> cutoffs;
    std::stringstream ss(a.str("cutoffs", "10,25,50"));
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try {
            cutoffs.push_back(std::stod(tok));
        } catch (...) {
        }
    }
    if (cutoffs.empty()) return fail("--cutoffs must list at least one number");

    const auto rep = agss::isochrone(g, origin, cutoffs);
    std::cout << "ISOCHRONES from node " << a.str("source", "0") << "  (" << fmt(rep.elapsed_ms)
              << " ms)\n"
              << std::left << std::setw(14) << "CUTOFF" << std::right << std::setw(12)
              << "REACHABLE" << std::setw(10) << "SHARE" << std::setw(12) << "HULL PTS" << "\n"
              << std::string(48, '-') << "\n";
    for (const auto& b : rep.bands) {
        const double share = 100.0 * static_cast<double>(b.nodes.size()) /
                             static_cast<double>(std::max(1, g.num_nodes()));
        std::cout << std::left << std::setw(14) << fmt(b.cutoff, 1) << std::right << std::setw(12)
                  << b.nodes.size() << std::setw(9) << fmt(share, 1) << "%" << std::setw(12)
                  << b.hull.size() << "\n";
    }

    if (a.has("out")) {
        std::ofstream out(a.str("out"));
        if (!out) return fail("cannot write " + a.str("out"));
        out << "{\n  \"origin\": " << g.external_id(origin) << ",\n  \"bands\": [\n";
        for (std::size_t i = 0; i < rep.bands.size(); ++i) {
            const auto& b = rep.bands[i];
            out << "    {\"cutoff\": " << agss::json::number(b.cutoff) << ", \"nodes\": "
                << b.nodes.size() << ", \"hull\": [";
            for (std::size_t j = 0; j < b.hull.size(); ++j) {
                if (j) out << ",";
                out << "[" << agss::json::number(b.hull[j].first) << ","
                    << agss::json::number(b.hull[j].second) << "]";
            }
            out << "]}" << (i + 1 < rep.bands.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        std::cout << "wrote " << a.str("out") << "\n";
    }
    return 0;
}

int cmd_kpaths(const Args& a) {
    if (a.str("graph").empty()) return fail("--graph is required");
    auto loaded = load_graph(a);
    if (!loaded) return fail(loaded.error().what());
    const auto& g = loaded.value().graph;

    bool o1 = false, o2 = false;
    const auto s = resolve(g, a.str("source", "0"), o1);
    const auto t = resolve(g, a.str("target", "1"), o2);
    if (!o1 || !o2) return fail("source or target not present in graph");

    const int k = static_cast<int>(a.num("k", 3));
    const auto rep = agss::k_shortest_paths(g, s, t, k);
    std::cout << "K SHORTEST ROUTES  (" << fmt(rep.elapsed_ms) << " ms)\n";
    if (rep.routes.empty()) {
        std::cout << "  no route exists\n";
        return 2;
    }
    const double best = rep.routes.front().cost;
    for (std::size_t i = 0; i < rep.routes.size(); ++i) {
        const auto& r = rep.routes[i];
        std::cout << "  route " << i + 1 << ": cost " << fmt(r.cost, 3) << "  ("
                  << r.path.size() - 1 << " hops, +"
                  << fmt(100.0 * (r.cost - best) / std::max(1e-9, best), 1) << "% vs best)\n";
    }
    return 0;
}

int cmd_transit(const Args& a) {
    const std::string dir = a.str("transit", "data/transit");
    auto net = agss::transit::load(dir);
    if (!net) return fail(net.error().what());
    auto network = net.value();

    if (a.has("city") || a.has("system")) {
        network = agss::transit::filter(network, a.has("city") ? a.str("city") : a.str("system"));
        if (network.stations.empty()) return fail("no stations matched that city/system");
    }

    if (a.has("list") || (!a.has("source") && !a.has("target"))) {
        std::map<std::string, std::pair<int, int>> per_system;
        for (const auto& s : network.stations) per_system[s.system].first++;
        std::map<std::int64_t, std::string> sys_of;
        for (const auto& s : network.stations) sys_of[s.id] = s.system;
        for (const auto& l : network.links) {
            auto it = sys_of.find(l.from);
            if (it != sys_of.end()) per_system[it->second].second++;
        }
        std::cout << "TRANSIT NETWORKS  (" << network.station_count() << " stations, "
                  << network.link_count() << " links, " << network.systems.size()
                  << " systems)\n\n"
                  << std::left << std::setw(34) << "SYSTEM" << std::right << std::setw(11)
                  << "STATIONS" << std::setw(9) << "LINKS" << "\n"
                  << std::string(54, '-') << "\n";
        for (const auto& [sys, counts] : per_system) {
            std::cout << std::left << std::setw(34) << sys << std::right << std::setw(11)
                      << counts.first << std::setw(9) << counts.second << "\n";
        }
        return 0;
    }

    agss::transit::BuildOptions bo;
    bo.transfer_radius_m = a.real("transfer-radius", bo.transfer_radius_m);
    bo.board_penalty_s = a.real("board-penalty", bo.board_penalty_s);
    bo.walk_speed_mps = a.real("walk-speed", bo.walk_speed_mps);
    bo.drive_speed_mps = a.real("drive-speed", bo.drive_speed_mps);

    agss::transit::MultiModal mm;
    if (a.has("graph")) {
        agss::LoadOptions lo;
        lo.space = agss::CoordSpace::Geographic;  // multi-modal requires lat/lon
        lo.lenient = a.has("lenient");
        auto road = agss::load_csv_dir(a.str("graph"), lo);
        if (!road) return fail(road.error().what());
        auto built = agss::transit::combine(road.value().graph, network, bo);
        if (!built) return fail(built.error().what());
        mm = std::move(built.value());
        std::cout << "multi-modal graph: " << mm.layers.road_node_count << " road nodes + "
                  << mm.layers.station_node_count << " stations, " << mm.graph.num_edges()
                  << " edges\n";
    } else {
        auto built = agss::transit::rail_only(network, bo);
        if (!built) return fail(built.error().what());
        mm = std::move(built.value());
        std::cout << "rail-only graph: " << mm.graph.num_nodes() << " stations, "
                  << mm.graph.num_edges() << " edges\n";
    }
    for (const auto& w : mm.warnings) std::cerr << "warning: " << w << "\n";

    // Resolve station names or ids.
    auto find_station = [&](const std::string& q) -> agss::NodeId {
        std::string lq = q;
        std::transform(lq.begin(), lq.end(), lq.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto& s : network.stations) {
            std::string ln = s.name;
            std::transform(ln.begin(), ln.end(), ln.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ln == lq || std::to_string(s.id) == q) {
                auto it = mm.layers.station_node.find(s.id);
                if (it != mm.layers.station_node.end()) return it->second;
            }
        }
        return agss::kInvalidNode;
    };

    const auto s = find_station(a.str("source"));
    const auto t = find_station(a.str("target"));
    if (s == agss::kInvalidNode) return fail("source station not found: " + a.str("source"));
    if (t == agss::kInvalidNode) return fail("target station not found: " + a.str("target"));

    auto alg = agss::Registry::instance().create(a.str("alg", "dijkstra"));
    if (!alg) return fail("unknown algorithm");
    const auto res = alg->run(mm.graph, s, t, {});
    if (!res.success) {
        std::cout << "no route found between those stations\n";
        return 2;
    }
    std::cout << "journey time : " << fmt(res.path_cost / 60.0, 1) << " minutes\n"
              << "hops         : " << res.path.size() - 1 << "\n"
              << "search time  : " << fmt(res.algorithm_ms) << " ms\n";

    std::map<std::int64_t, const agss::transit::Station*> by_id;
    for (const auto& st : network.stations) by_id[st.id] = &st;
    std::cout << "\nroute:\n";
    for (agss::NodeId n : res.path) {
        const auto ext = mm.graph.external_id(n);
        if (ext >= 1'000'000'000LL) {
            auto it = by_id.find(ext - 1'000'000'000LL);
            if (it != by_id.end()) {
                std::cout << "  [rail] " << it->second->name << "  (" << it->second->system
                          << ")\n";
                continue;
            }
        }
        std::cout << "  [road] node " << ext << "\n";
    }
    return 0;
}

int cmd_maps(const Args& a) {
    const std::string root = a.str("dir", "data/maps");
    std::cout << "maps under " << root << ":\n";
    // Deliberately no directory-walk dependency: probe the known layout.
    for (const auto& name : {"Small_Campus", "Mumbai_Pune_Expy", "Indian_Grid", "Delhi_NCR",
                             "Bengaluru_Traffic"}) {
        agss::LoadOptions o;
        o.lenient = true;
        auto r = agss::load_csv_dir(root + "/" + name, o);
        if (r) {
            std::cout << "  " << std::left << std::setw(22) << name << r.value().graph.num_nodes()
                      << " nodes, " << r.value().graph.num_edges() << " edges\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args a = parse_args(argc, argv);
    if (a.command.empty() || a.command == "help" || a.has("help")) {
        print_usage();
        return a.command.empty() ? 1 : 0;
    }
    try {
        if (a.command == "route") return cmd_route(a);
        if (a.command == "race") return cmd_race(a);
        if (a.command == "verify") return cmd_verify(a);
        if (a.command == "bench") return cmd_bench(a);
        if (a.command == "analyze") return cmd_analyze(a);
        if (a.command == "isochrone") return cmd_isochrone(a);
        if (a.command == "kpaths") return cmd_kpaths(a);
        if (a.command == "transit") return cmd_transit(a);
        if (a.command == "maps") return cmd_maps(a);
        if (a.command == "algorithms") return cmd_algorithms();
    } catch (const std::exception& e) {
        return fail(std::string("unexpected: ") + e.what());
    }
    std::cerr << "unknown command '" << a.command << "'\n\n";
    print_usage();
    return 1;
}
