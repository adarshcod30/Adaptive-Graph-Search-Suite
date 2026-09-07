#include "agss/json.hpp"

#include <cmath>
#include <cstdio>

namespace agss::json {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        // clang-format off
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            // clang-format on
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

void write_graph(std::ostream& out, const Graph& g, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const bool geo = g.coord_space() == CoordSpace::Geographic;
    out << "{\n" << pad << "  \"coordSpace\": \"" << (geo ? "geographic" : "planar") << "\",\n";
    out << pad << "  \"nodes\": [";
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        if (u) out << ',';
        out << "\n"
            << pad << "    {\"id\":" << u << ",\"ext\":" << g.external_id(u)
            << ",\"x\":" << number(g.x(u)) << ",\"y\":" << number(g.y(u)) << "}";
    }
    out << "\n" << pad << "  ],\n" << pad << "  \"edges\": [";
    bool first = true;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (!first) out << ',';
            first = false;
            out << "\n"
                << pad << "    {\"id\":" << e << ",\"u\":" << u << ",\"v\":" << g.edge_target(e)
                << ",\"w\":" << number(g.edge_weight(e)) << "}";
        }
    }
    out << "\n" << pad << "  ]\n" << pad << "}";
}

void write_trace(std::ostream& out, const TraceDocument& doc) {
    const auto& res = *doc.result;
    out << "{\n";
    out << "  \"formatVersion\": 2,\n";
    out << "  \"metadata\": {\n";
    out << "    \"algorithm\": \"" << escape(res.algorithm) << "\",\n";
    out << "    \"timeComplexity\": \"" << escape(res.time_complexity) << "\",\n";
    out << "    \"spaceComplexity\": \"" << escape(res.space_complexity) << "\",\n";
    out << "    \"algorithmMs\": " << number(res.algorithm_ms) << ",\n";
    out << "    \"traceMs\": " << number(doc.trace_ms) << ",\n";
    out << "    \"nodesExpanded\": " << res.nodes_expanded << ",\n";
    out << "    \"edgesRelaxed\": " << res.edges_relaxed << ",\n";
    out << "    \"pathCost\": " << number(res.path_cost) << ",\n";
    out << "    \"pathLength\": " << res.path.size() << ",\n";
    out << "    \"success\": " << (res.success ? "true" : "false") << "\n";
    out << "  },\n";

    if (doc.graph) {
        out << "  \"graph\": ";
        write_graph(out, *doc.graph, 2);
        out << ",\n";
    }

    out << "  \"path\": [";
    for (std::size_t i = 0; i < res.path.size(); ++i) {
        if (i) out << ',';
        out << res.path[i];
    }
    out << "],\n";

    if (doc.closures && !doc.closures->empty()) {
        out << "  \"closedEdges\": [";
        bool first = true;
        for (EdgeId e = 0; doc.graph && e < doc.graph->num_edges(); ++e) {
            if (!doc.closures->is_closed(e)) continue;
            if (!first) out << ',';
            first = false;
            out << e;
        }
        out << "],\n";
    }

    // Delta stream: [op, node, parent] triples. op 0=discover, 1=expand,
    // 2=relax. The client replays these to rebuild any frame, so the file
    // stays O(V + E) instead of the O(V^2) full-snapshot format it replaced.
    out << "  \"eventLegend\": {\"0\": \"discover\", \"1\": \"expand\", \"2\": \"relax\"},\n";
    out << "  \"events\": [";
    if (doc.trace) {
        const auto& evs = doc.trace->events();
        for (std::size_t i = 0; i < evs.size(); ++i) {
            if (i) out << ',';
            if (i % 16 == 0) out << "\n    ";
            out << '[' << static_cast<int>(evs[i].op) << ',' << evs[i].node << ',' << evs[i].parent
                << ']';
        }
        if (!evs.empty()) out << "\n  ";
    }
    out << "]\n}\n";
}

}  // namespace agss::json
