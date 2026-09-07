#include "agss/loader.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string_view>

#include "agss/graph_builder.hpp"

namespace agss {
namespace {

std::string_view trim(std::string_view s) {
    const auto* ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

/// Non-throwing integer parse. std::stoi throws std::invalid_argument on
/// garbage, which previously propagated out of main() and aborted the process
/// (SIGABRT) on any malformed CSV.
bool parse_int(std::string_view s, std::int64_t& out) {
    s = trim(s);
    if (s.empty()) return false;
    const std::string tmp(s);
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(tmp.c_str(), &end, 10);
    if (errno != 0 || end == tmp.c_str() || *end != '\0') return false;
    out = static_cast<std::int64_t>(v);
    return true;
}

bool parse_double(std::string_view s, double& out) {
    s = trim(s);
    if (s.empty()) return false;
    const std::string tmp(s);
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (errno != 0 || end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

std::vector<std::string_view> split(std::string_view line, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = line.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

/// True if the row looks like a header (first field is not a number).
bool is_header(const std::vector<std::string_view>& fields) {
    std::int64_t dummy = 0;
    return !fields.empty() && !parse_int(fields[0], dummy);
}

}  // namespace

Result<LoadReport> load_csv(const std::string& nodes_path, const std::string& edges_path,
                            const LoadOptions& opts) {
    LoadReport report;
    GraphBuilder builder(opts.space);

    std::ifstream fn(nodes_path);
    if (!fn) return Error{"cannot open nodes file", nodes_path};

    std::string line;
    int lineno = 0;
    bool first = true;
    while (std::getline(fn, line)) {
        ++lineno;
        const auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        auto fields = split(trimmed, ',');
        if (first && is_header(fields)) {
            first = false;
            continue;
        }
        first = false;

        if (fields.size() < 3) {
            if (opts.lenient) {
                report.warnings.push_back(nodes_path + ":" + std::to_string(lineno) +
                                          ": expected 3 fields, skipping");
                continue;
            }
            return Error{"expected 3 fields (id,x,y), got " + std::to_string(fields.size()),
                         nodes_path, lineno};
        }
        std::int64_t id = 0;
        double x = 0.0, y = 0.0;
        if (!parse_int(fields[0], id) || !parse_double(fields[1], x) ||
            !parse_double(fields[2], y)) {
            if (opts.lenient) {
                report.warnings.push_back(nodes_path + ":" + std::to_string(lineno) +
                                          ": unparseable row, skipping");
                continue;
            }
            return Error{"unparseable node row", nodes_path, lineno};
        }
        builder.add_node(id, x, y);
    }

    if (builder.node_count() == 0) return Error{"file declares no nodes", nodes_path};

    std::ifstream fe(edges_path);
    if (!fe) return Error{"cannot open edges file", edges_path};

    lineno = 0;
    first = true;
    while (std::getline(fe, line)) {
        ++lineno;
        const auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        auto fields = split(trimmed, ',');
        if (first && is_header(fields)) {
            first = false;
            continue;
        }
        first = false;

        if (fields.size() < 2) {
            if (opts.lenient) {
                report.warnings.push_back(edges_path + ":" + std::to_string(lineno) +
                                          ": expected at least 2 fields, skipping");
                continue;
            }
            return Error{"expected fields (u,v[,w]), got " + std::to_string(fields.size()),
                         edges_path, lineno};
        }
        std::int64_t u = 0, v = 0;
        double w = 1.0;
        if (!parse_int(fields[0], u) || !parse_int(fields[1], v)) {
            if (opts.lenient) {
                report.warnings.push_back(edges_path + ":" + std::to_string(lineno) +
                                          ": unparseable endpoints, skipping");
                continue;
            }
            return Error{"unparseable edge endpoints", edges_path, lineno};
        }
        if (fields.size() >= 3 && !trim(fields[2]).empty() && !parse_double(fields[2], w)) {
            if (opts.lenient) {
                report.warnings.push_back(edges_path + ":" + std::to_string(lineno) +
                                          ": unparseable weight, skipping");
                continue;
            }
            return Error{"unparseable edge weight", edges_path, lineno};
        }

        auto added = builder.add_edge(u, v, w, opts.allow_negative_weights);
        if (!added) {
            if (opts.lenient) {
                report.warnings.push_back(edges_path + ":" + std::to_string(lineno) + ": " +
                                          added.error().message + ", skipping");
                continue;
            }
            return Error{added.error().message, edges_path, lineno};
        }
    }

    report.graph = builder.build();
    return report;
}

Result<LoadReport> load_csv_dir(const std::string& dir, const LoadOptions& opts) {
    return load_csv(dir + "/nodes.csv", dir + "/edges.csv", opts);
}

}  // namespace agss
