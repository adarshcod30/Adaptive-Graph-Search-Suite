// Python bindings.
//
// The audience for this is researchers and analysts, who live in Python and
// will not link a C++ library to try an idea. Exposing the engine here costs
// one file and multiplies who can use it.
//
// The API is deliberately Pythonic rather than a mechanical mirror of the C++
// one: NumPy-friendly sequences in, dataclass-like objects out, and errors as
// exceptions instead of Result<T>.
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <stdexcept>

#include "agss/algorithm.hpp"
#include "agss/alt.hpp"
#include "agss/analysis.hpp"
#include "agss/contraction_hierarchy.hpp"
#include "agss/customizable_ch.hpp"
#include "agss/directions.hpp"
#include "agss/graph_builder.hpp"
#include "agss/isochrone.hpp"
#include "agss/kdtree.hpp"
#include "agss/kshortest.hpp"
#include "agss/loader.hpp"
#include "agss/time_dependent.hpp"
#include "agss/transit.hpp"

namespace py = pybind11;
using namespace agss;

namespace {

/// Own the graph so Python can keep indices alive independently of the loader.
struct PyGraph {
    Graph graph;
    std::optional<KdTree> index;

    const KdTree& spatial() {
        if (!index) index.emplace(graph);
        return *index;
    }
};

std::shared_ptr<PyGraph> load_dir(const std::string& dir, bool geographic, bool lenient) {
    LoadOptions o;
    o.space = geographic ? CoordSpace::Geographic : CoordSpace::Planar;
    o.lenient = lenient;
    auto r = load_csv_dir(dir, o);
    if (!r) throw std::runtime_error(r.error().what());
    auto out = std::make_shared<PyGraph>();
    out->graph = std::move(r.value().graph);
    return out;
}

std::shared_ptr<PyGraph> from_arrays(const py::sequence& nodes, const py::sequence& edges,
                                     bool geographic) {
    GraphBuilder b(geographic ? CoordSpace::Geographic : CoordSpace::Planar);
    for (const auto& item : nodes) {
        auto t = item.cast<py::sequence>();
        if (py::len(t) < 3) throw std::invalid_argument("each node needs (id, x, y)");
        b.add_node(t[0].cast<std::int64_t>(), t[1].cast<double>(), t[2].cast<double>());
    }
    for (const auto& item : edges) {
        auto t = item.cast<py::sequence>();
        if (py::len(t) < 2) throw std::invalid_argument("each edge needs (u, v[, weight])");
        const double w = py::len(t) >= 3 ? t[2].cast<double>() : 1.0;
        auto added = b.add_edge(t[0].cast<std::int64_t>(), t[1].cast<std::int64_t>(), w);
        if (!added) throw std::invalid_argument(added.error().message);
    }
    auto out = std::make_shared<PyGraph>();
    out->graph = b.build();
    return out;
}

SearchResult run_named(PyGraph& g, const std::string& alg, NodeId s, NodeId t) {
    auto a = Registry::instance().create(alg);
    if (!a) throw std::invalid_argument("unknown algorithm: " + alg);
    return a->run(g.graph, s, t, {});
}

}  // namespace

PYBIND11_MODULE(_agss, m) {
    m.doc() = "Adaptive Graph Search Suite - pathfinding and network analysis on road graphs";

    // Registered first: pybind11 resolves default arguments at binding time,
    // so a type used as one must already exist. Declaring it later produced
    // only "could not convert default argument into a Python object".
    py::class_<SearchOptions>(m, "SearchOptions").def(py::init<>());

    py::class_<SearchResult>(m, "SearchResult")
        .def_readonly("algorithm", &SearchResult::algorithm)
        .def_readonly("success", &SearchResult::success)
        .def_readonly("path", &SearchResult::path)
        .def_readonly("cost", &SearchResult::path_cost)
        .def_readonly("milliseconds", &SearchResult::algorithm_ms)
        .def_readonly("nodes_expanded", &SearchResult::nodes_expanded)
        .def_readonly("edges_relaxed", &SearchResult::edges_relaxed)
        .def_readonly("time_complexity", &SearchResult::time_complexity)
        .def_readonly("space_complexity", &SearchResult::space_complexity)
        .def("__repr__", [](const SearchResult& r) {
            return "<SearchResult " + r.algorithm + " success=" + (r.success ? "True" : "False") +
                   " cost=" + std::to_string(r.path_cost) + ">";
        });

    py::class_<PyGraph, std::shared_ptr<PyGraph>>(m, "Graph")
        .def_property_readonly("num_nodes", [](PyGraph& g) { return g.graph.num_nodes(); })
        .def_property_readonly("num_edges", [](PyGraph& g) { return g.graph.num_edges(); })
        .def_property_readonly(
            "geographic",
            [](PyGraph& g) { return g.graph.coord_space() == CoordSpace::Geographic; })
        .def_property_readonly("heuristic_admissible",
                               [](PyGraph& g) { return g.graph.heuristic_is_admissible(); })
        .def(
            "coordinates",
            [](PyGraph& g) {
                const auto n = static_cast<std::size_t>(g.graph.num_nodes());
                py::array_t<double> xs(n), ys(n);
                auto x = xs.mutable_unchecked<1>();
                auto y = ys.mutable_unchecked<1>();
                for (std::size_t i = 0; i < n; ++i) {
                    x(i) = g.graph.x(static_cast<NodeId>(i));
                    y(i) = g.graph.y(static_cast<NodeId>(i));
                }
                return py::make_tuple(xs, ys);
            },
            "Node coordinates as a pair of NumPy arrays.")
        .def("edge_list",
             [](PyGraph& g) {
                 std::vector<std::tuple<NodeId, NodeId, double>> out;
                 out.reserve(static_cast<std::size_t>(g.graph.num_edges()));
                 for (NodeId u = 0; u < g.graph.num_nodes(); ++u) {
                     for (EdgeId e = g.graph.edge_begin(u); e < g.graph.edge_end(u); ++e) {
                         out.emplace_back(u, g.graph.edge_target(e), g.graph.edge_weight(e));
                     }
                 }
                 return out;
             })
        .def("external_id", [](PyGraph& g, NodeId v) { return g.graph.external_id(v); })
        .def("lookup", [](PyGraph& g, std::int64_t ext) { return g.graph.lookup(ext); })
        .def(
            "nearest", [](PyGraph& g, double x, double y) { return g.spatial().nearest(x, y); },
            py::arg("x"), py::arg("y"),
            "Nearest node to a coordinate, via a k-d tree built on first use.")
        .def("route", &run_named, py::arg("algorithm"), py::arg("source"), py::arg("target"))
        .def("__repr__", [](PyGraph& g) {
            return "<agss.Graph " + std::to_string(g.graph.num_nodes()) + " nodes, " +
                   std::to_string(g.graph.num_edges()) + " edges>";
        });

    m.def("load", &load_dir, py::arg("directory"), py::arg("geographic") = false,
          py::arg("lenient") = true, "Load nodes.csv and edges.csv from a directory.");
    m.def("from_arrays", &from_arrays, py::arg("nodes"), py::arg("edges"),
          py::arg("geographic") = false,
          "Build a graph from (id, x, y) and (u, v, weight) sequences.");
    m.def(
        "algorithms", [] { return Registry::instance().keys(); },
        "Names accepted by Graph.route().");

    // ---- Contraction Hierarchies -----------------------------------------
    py::class_<ContractionHierarchy>(m, "ContractionHierarchy")
        .def(py::init<>())
        .def(
            "build",
            [](ContractionHierarchy& ch, std::shared_ptr<PyGraph> g, double budget_ms) {
                ContractionHierarchy::BuildOptions o;
                o.budget_ms = budget_ms;
                ch.build(g->graph, o);
            },
            py::arg("graph"), py::arg("budget_ms") = 20000.0,
            py::keep_alive<1, 2>())  // the hierarchy borrows the graph
        .def("query", &ContractionHierarchy::query, py::arg("source"), py::arg("target"),
             py::arg("options") = SearchOptions{})
        .def_property_readonly("ready", &ContractionHierarchy::ready)
        .def_property_readonly("build_ms",
                               [](const ContractionHierarchy& c) { return c.stats().build_ms; })
        .def_property_readonly("shortcuts",
                               [](const ContractionHierarchy& c) { return c.stats().shortcuts; })
        .def_property_readonly("edge_growth",
                               [](const ContractionHierarchy& c) { return c.stats().edge_growth; });

    py::class_<CustomizableCH>(m, "CustomizableCH")
        .def(py::init<>())
        .def(
            "build",
            [](CustomizableCH& c, std::shared_ptr<PyGraph> g, double budget_ms) {
                CustomizableCH::BuildOptions o;
                o.budget_ms = budget_ms;
                c.build(g->graph, o);
            },
            py::arg("graph"), py::arg("budget_ms") = 30000.0, py::keep_alive<1, 2>())
        .def("customize", py::overload_cast<>(&CustomizableCH::customize),
             "Use the graph's own weights.")
        .def("customize", py::overload_cast<const std::vector<double>&>(&CustomizableCH::customize),
             py::arg("weights"), "Apply one weight per edge, in edge_list() order.")
        .def("query", &CustomizableCH::query, py::arg("source"), py::arg("target"),
             py::arg("options") = SearchOptions{})
        .def_property_readonly("ready", &CustomizableCH::ready)
        .def_property_readonly("build_ms",
                               [](const CustomizableCH& c) { return c.stats().build_ms; })
        .def_property_readonly("customize_ms",
                               [](const CustomizableCH& c) { return c.stats().customize_ms; });

    py::class_<AltIndex>(m, "AltIndex")
        .def(py::init<>())
        .def(
            "build",
            [](AltIndex& a, std::shared_ptr<PyGraph> g, int landmarks) {
                AltIndex::BuildOptions o;
                o.landmark_count = landmarks;
                a.build(g->graph, o);
            },
            py::arg("graph"), py::arg("landmarks") = 12, py::keep_alive<1, 2>())
        .def("query", &AltIndex::query, py::arg("source"), py::arg("target"),
             py::arg("options") = SearchOptions{})
        .def("lower_bound", &AltIndex::lower_bound, py::arg("node"), py::arg("target"))
        .def_property_readonly("landmarks", &AltIndex::landmarks)
        .def_property_readonly("build_ms", [](const AltIndex& a) { return a.stats().build_ms; });

    // ---- Time-dependent ---------------------------------------------------
    py::class_<TimeDependentModel>(m, "TimeDependentModel")
        .def(py::init([](std::shared_ptr<PyGraph> g, double speed, std::uint64_t seed) {
                 return std::make_unique<TimeDependentModel>(g->graph, speed, seed);
             }),
             py::arg("graph"), py::arg("free_flow_speed_mps") = 11.1, py::arg("seed") = 20260908,
             py::keep_alive<1, 2>())
        .def("travel_time", &TimeDependentModel::travel_time, py::arg("edge"),
             py::arg("departure_seconds"))
        .def("free_flow_time", &TimeDependentModel::free_flow_time, py::arg("edge"));

    py::class_<TimeDependentResult>(m, "TimeDependentResult")
        .def_readonly("search", &TimeDependentResult::search)
        .def_readonly("departure", &TimeDependentResult::departure)
        .def_readonly("arrival", &TimeDependentResult::arrival)
        .def_readonly("duration", &TimeDependentResult::duration)
        .def_readonly("free_flow_duration", &TimeDependentResult::free_flow_duration);

    py::class_<DepartureScan>(m, "DepartureScan")
        .def_readonly("departures", &DepartureScan::departures)
        .def_readonly("durations", &DepartureScan::durations)
        .def_readonly("best_departure", &DepartureScan::best_departure)
        .def_readonly("best_duration", &DepartureScan::best_duration)
        .def_readonly("worst_departure", &DepartureScan::worst_departure)
        .def_readonly("worst_duration", &DepartureScan::worst_duration);

    m.def(
        "earliest_arrival",
        [](const TimeDependentModel& model, NodeId s, NodeId t, double departure) {
            return earliest_arrival(model, s, t, departure, {});
        },
        py::arg("model"), py::arg("source"), py::arg("target"), py::arg("departure_seconds"));
    m.def(
        "scan_departures",
        [](const TimeDependentModel& model, NodeId s, NodeId t, int samples) {
            return scan_departures(model, s, t, samples);
        },
        py::arg("model"), py::arg("source"), py::arg("target"), py::arg("samples") = 24);

    // ---- Analysis ---------------------------------------------------------
    m.def(
        "bridges",
        [](std::shared_ptr<PyGraph> g) {
            const auto rep = analysis::find_bridges(g->graph);
            std::vector<std::tuple<NodeId, NodeId, std::int64_t>> out;
            out.reserve(rep.bridges.size());
            for (const auto& b : rep.bridges) out.emplace_back(b.u, b.v, b.isolated_nodes);
            return py::make_tuple(out, rep.articulation_points, rep.component_count);
        },
        py::arg("graph"), "Returns (bridges, articulation_points, component_count).");
    m.def(
        "betweenness_centrality",
        [](std::shared_ptr<PyGraph> g, std::int64_t samples) {
            return analysis::betweenness_centrality(g->graph, samples).betweenness;
        },
        py::arg("graph"), py::arg("samples") = 0,
        "Brandes betweenness; samples=0 computes it exactly.");
    m.def(
        "strongly_connected_components",
        [](std::shared_ptr<PyGraph> g) {
            return analysis::strongly_connected_components(g->graph).component_of;
        },
        py::arg("graph"));
    m.def(
        "minimum_spanning_tree",
        [](std::shared_ptr<PyGraph> g) {
            const auto rep = analysis::minimum_spanning_tree(g->graph);
            return py::make_tuple(rep.edges, rep.total_weight);
        },
        py::arg("graph"));
    m.def(
        "max_flow",
        [](std::shared_ptr<PyGraph> g, NodeId s, NodeId t) {
            const auto rep = analysis::max_flow(g->graph, s, t);
            return py::make_tuple(rep.max_flow, rep.min_cut);
        },
        py::arg("graph"), py::arg("source"), py::arg("sink"));
    m.def(
        "k_shortest_paths",
        [](std::shared_ptr<PyGraph> g, NodeId s, NodeId t, int k) {
            const auto rep = k_shortest_paths(g->graph, s, t, k);
            std::vector<std::pair<std::vector<NodeId>, double>> out;
            for (const auto& r : rep.routes) out.emplace_back(r.path, r.cost);
            return out;
        },
        py::arg("graph"), py::arg("source"), py::arg("target"), py::arg("k") = 3);
    m.def(
        "isochrone",
        [](std::shared_ptr<PyGraph> g, NodeId origin, const std::vector<double>& cutoffs) {
            const auto rep = agss::isochrone(g->graph, origin, cutoffs);
            py::list bands;
            for (const auto& b : rep.bands) {
                bands.append(py::make_tuple(b.cutoff, b.nodes, b.hull));
            }
            return py::make_tuple(bands, rep.cost_to);
        },
        py::arg("graph"), py::arg("origin"), py::arg("cutoffs"));
    m.def(
        "directions",
        [](std::shared_ptr<PyGraph> g, const std::vector<NodeId>& path) {
            const auto d = build_directions(g->graph, path);
            std::vector<std::tuple<std::string, NodeId, double>> out;
            for (const auto& s : d.steps) {
                out.emplace_back(to_string(s.maneuver), s.at, s.distance);
            }
            return py::make_tuple(out, d.total_distance);
        },
        py::arg("graph"), py::arg("path"));
}
