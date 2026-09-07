#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "agss/error.hpp"
#include "agss/graph.hpp"

namespace agss::transit {

struct Station {
    std::int64_t id = 0;
    std::string name;
    std::string system;  ///< e.g. "Delhi Metro", "Namma Metro"
    std::string city;
    double lat = 0.0;
    double lon = 0.0;
};

struct Link {
    std::int64_t from = 0;
    std::int64_t to = 0;
    std::string line;     ///< e.g. "Blue Line"
    double seconds = 0.0; ///< in-vehicle travel time
};

struct Network {
    std::vector<Station> stations;
    std::vector<Link> links;
    std::vector<std::string> systems;  ///< distinct systems present

    std::size_t station_count() const { return stations.size(); }
    std::size_t link_count() const { return links.size(); }
};

/// Load `<dir>/stations.csv` and `<dir>/links.csv`.
/// stations.csv: id,name,system,city,lat,lon
/// links.csv:    from,to,line,seconds
Result<Network> load(const std::string& dir);

/// Restrict a network to one system or city (case-insensitive substring).
Network filter(const Network& net, const std::string& needle);

/// How the layers of a combined graph are laid out.
struct LayerInfo {
    NodeId road_node_count = 0;      ///< dense ids [0, road_node_count) are road
    NodeId station_node_count = 0;   ///< [road_node_count, total) are stations
    std::vector<std::int64_t> station_external_ids;  ///< index by (id - road_node_count)
    std::unordered_map<std::int64_t, NodeId> station_node;
};

struct BuildOptions {
    double walk_speed_mps = 1.35;      ///< ~4.9 km/h
    double drive_speed_mps = 8.33;     ///< ~30 km/h, city arterial average
    double transfer_radius_m = 400.0;  ///< how far a rider will walk to a station
    double board_penalty_s = 180.0;    ///< wait + platform time when boarding
    int max_transfers_per_station = 4;
};

struct MultiModal {
    Graph graph;
    LayerInfo layers;
    std::vector<std::string> warnings;
};

/// Combine a road graph and a transit network into one time-weighted graph.
///
/// Every edge weight becomes seconds, so a plain Dijkstra over the result is
/// already a correct multi-modal query -- no special algorithm needed. Road
/// edges convert distance to time at `drive_speed_mps`; rail links carry their
/// own timings; and each station is stitched to nearby road nodes with walking
/// edges plus a boarding penalty, which is what stops the router from treating
/// a metro hop as free teleportation.
///
/// `road` must be in geographic coordinates.
Result<MultiModal> combine(const Graph& road, const Network& net, const BuildOptions& opts = {});

/// Build a graph from the transit network alone (rail-only routing).
Result<MultiModal> rail_only(const Network& net, const BuildOptions& opts = {});

}  // namespace agss::transit
