#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "agss/graph_builder.hpp"
#include "agss/kdtree.hpp"
#include "agss/transit.hpp"

namespace agss::transit {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// Split on commas, honouring double-quoted fields -- station names such as
/// "Rajiv Chowk, Blue Line" contain commas.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

bool parse_i64(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_f64(const std::string& s, double& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

}  // namespace

Result<Network> load(const std::string& dir) {
    Network net;

    const std::string spath = dir + "/stations.csv";
    std::ifstream fs(spath);
    if (!fs) return Error{"cannot open stations file", spath};

    std::string line;
    int lineno = 0;
    bool first = true;
    while (std::getline(fs, line)) {
        ++lineno;
        if (trim(line).empty()) continue;
        auto f = split_csv(line);
        if (first) {
            first = false;
            std::int64_t probe = 0;
            if (!f.empty() && !parse_i64(f[0], probe)) continue;  // header
        }
        if (f.size() < 6) return Error{"expected id,name,system,city,lat,lon", spath, lineno};
        Station st;
        if (!parse_i64(f[0], st.id) || !parse_f64(f[4], st.lat) || !parse_f64(f[5], st.lon)) {
            return Error{"unparseable station row", spath, lineno};
        }
        st.name = f[1];
        st.system = f[2];
        st.city = f[3];
        net.stations.push_back(std::move(st));
    }
    if (net.stations.empty()) return Error{"no stations declared", spath};

    const std::string lpath = dir + "/links.csv";
    std::ifstream fl(lpath);
    if (!fl) return Error{"cannot open links file", lpath};

    std::unordered_map<std::int64_t, bool> known;
    for (const auto& s : net.stations) known[s.id] = true;

    lineno = 0;
    first = true;
    while (std::getline(fl, line)) {
        ++lineno;
        if (trim(line).empty()) continue;
        auto f = split_csv(line);
        if (first) {
            first = false;
            std::int64_t probe = 0;
            if (!f.empty() && !parse_i64(f[0], probe)) continue;
        }
        if (f.size() < 4) return Error{"expected from,to,line,seconds", lpath, lineno};
        Link lk;
        if (!parse_i64(f[0], lk.from) || !parse_i64(f[1], lk.to) || !parse_f64(f[3], lk.seconds)) {
            return Error{"unparseable link row", lpath, lineno};
        }
        if (!known.count(lk.from) || !known.count(lk.to)) {
            return Error{"link references unknown station", lpath, lineno};
        }
        lk.line = f[2];
        net.links.push_back(std::move(lk));
    }

    std::vector<std::string> sys;
    for (const auto& s : net.stations) sys.push_back(s.system);
    std::sort(sys.begin(), sys.end());
    sys.erase(std::unique(sys.begin(), sys.end()), sys.end());
    net.systems = std::move(sys);
    return net;
}

Network filter(const Network& net, const std::string& needle) {
    const std::string key = lower(needle);
    Network out;
    std::unordered_map<std::int64_t, bool> keep;

    // Exact matches win over substring ones. A pure substring filter bleeds
    // between systems that share a name -- "Mumbai Metro" also selected "Navi
    // Mumbai Metro", and "Delhi" swept in the Delhi NCR RRTS -- so every count
    // shifted whenever a new city was imported. With exact match taking
    // priority, naming a system selects that system and nothing else, while a
    // partial name typed at the CLI still falls back to substring.
    const bool exact = std::any_of(net.stations.begin(), net.stations.end(), [&](const Station& s) {
        return lower(s.system) == key || lower(s.city) == key;
    });

    for (const auto& s : net.stations) {
        const bool hit = exact ? (lower(s.system) == key || lower(s.city) == key)
                               : (lower(s.system).find(key) != std::string::npos ||
                                  lower(s.city).find(key) != std::string::npos);
        if (hit) {
            keep[s.id] = true;
            out.stations.push_back(s);
        }
    }
    for (const auto& l : net.links) {
        if (keep.count(l.from) && keep.count(l.to)) out.links.push_back(l);
    }
    std::vector<std::string> sys;
    for (const auto& s : out.stations) sys.push_back(s.system);
    std::sort(sys.begin(), sys.end());
    sys.erase(std::unique(sys.begin(), sys.end()), sys.end());
    out.systems = std::move(sys);
    return out;
}

namespace {

/// Station external ids are offset so they cannot collide with road node ids.
constexpr std::int64_t kStationIdBase = 1'000'000'000LL;

void add_rail(GraphBuilder& b, const Network& net, const BuildOptions& opts,
              std::vector<std::string>& warnings) {
    for (const auto& l : net.links) {
        const double secs = l.seconds > 0.0 ? l.seconds : 60.0;
        // Boarding cost is charged on the transfer edges, not here, so riding
        // several stops on one line is not penalised per hop.
        auto r = b.add_undirected(kStationIdBase + l.from, kStationIdBase + l.to, secs);
        if (!r) warnings.push_back(r.error().message);
    }
    (void)opts;
}

}  // namespace

Result<MultiModal> rail_only(const Network& net, const BuildOptions& opts) {
    if (net.stations.empty()) return Error{"transit network has no stations"};
    MultiModal mm;
    GraphBuilder b(CoordSpace::Geographic);
    for (const auto& s : net.stations) {
        b.add_node(kStationIdBase + s.id, s.lon, s.lat);
    }
    add_rail(b, net, opts, mm.warnings);
    mm.graph = b.build();
    mm.layers.road_node_count = 0;
    mm.layers.station_node_count = mm.graph.num_nodes();
    for (const auto& s : net.stations) {
        const NodeId n = mm.graph.lookup(kStationIdBase + s.id);
        if (n != kInvalidNode) {
            mm.layers.station_node[s.id] = n;
            mm.layers.station_external_ids.push_back(s.id);
        }
    }
    return mm;
}

Result<MultiModal> combine(const Graph& road, const Network& net, const BuildOptions& opts) {
    if (road.coord_space() != CoordSpace::Geographic) {
        return Error{"multi-modal routing needs a geographic road graph (lat/lon)"};
    }
    if (net.stations.empty()) return Error{"transit network has no stations"};

    MultiModal mm;
    GraphBuilder b(CoordSpace::Geographic);

    // Layer 1: road nodes, keeping their original external ids.
    for (NodeId u = 0; u < road.num_nodes(); ++u) {
        b.add_node(road.external_id(u), road.lon(u), road.lat(u));
    }
    // Road weights are metres; convert to seconds so both layers share a unit.
    for (NodeId u = 0; u < road.num_nodes(); ++u) {
        for (EdgeId e = road.edge_begin(u); e < road.edge_end(u); ++e) {
            const double secs = road.edge_weight(e) / opts.drive_speed_mps;
            auto r = b.add_edge(road.external_id(u), road.external_id(road.edge_target(e)), secs);
            if (!r) mm.warnings.push_back(r.error().message);
        }
    }

    // Layer 2: stations.
    for (const auto& s : net.stations) b.add_node(kStationIdBase + s.id, s.lon, s.lat);
    add_rail(b, net, opts, mm.warnings);

    // Stitch: walking transfers from each station to nearby road nodes.
    KdTree index(road);
    std::size_t stitched = 0;
    for (const auto& s : net.stations) {
        auto near = index.within(s.lon, s.lat, opts.transfer_radius_m);
        if (near.empty()) {
            const NodeId n = index.nearest(s.lon, s.lat);
            if (n != kInvalidNode) near.push_back(n);
        }
        std::sort(near.begin(), near.end(), [&](NodeId a, NodeId c) {
            return geo::equirectangular(s.lat, s.lon, road.lat(a), road.lon(a)) <
                   geo::equirectangular(s.lat, s.lon, road.lat(c), road.lon(c));
        });
        if (static_cast<int>(near.size()) > opts.max_transfers_per_station) {
            near.resize(static_cast<std::size_t>(opts.max_transfers_per_station));
        }
        for (NodeId r : near) {
            const double metres = geo::equirectangular(s.lat, s.lon, road.lat(r), road.lon(r));
            const double walk = metres / opts.walk_speed_mps;
            // Boarding penalty applies on the way in; stepping off is free.
            auto in =
                b.add_edge(road.external_id(r), kStationIdBase + s.id, walk + opts.board_penalty_s);
            auto out = b.add_edge(kStationIdBase + s.id, road.external_id(r), walk);
            if (!in) mm.warnings.push_back(in.error().message);
            if (!out) mm.warnings.push_back(out.error().message);
            ++stitched;
        }
    }
    if (stitched == 0) {
        mm.warnings.push_back(
            "no station fell within transfer_radius_m of any road node -- the rail layer is "
            "isolated from the road layer");
    }

    mm.graph = b.build();
    mm.layers.road_node_count = road.num_nodes();
    mm.layers.station_node_count = mm.graph.num_nodes() - road.num_nodes();
    for (const auto& s : net.stations) {
        const NodeId n = mm.graph.lookup(kStationIdBase + s.id);
        if (n != kInvalidNode) {
            mm.layers.station_node[s.id] = n;
            mm.layers.station_external_ids.push_back(s.id);
        }
    }
    return mm;
}

}  // namespace agss::transit
