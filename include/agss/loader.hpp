#pragma once

#include <string>
#include <vector>

#include "agss/error.hpp"
#include "agss/graph.hpp"

namespace agss {

struct LoadOptions {
    CoordSpace space = CoordSpace::Planar;
    bool allow_negative_weights = false;
    /// Skip malformed rows instead of failing the whole load. Skipped rows are
    /// reported through `warnings`.
    bool lenient = false;
};

struct LoadReport {
    Graph graph;
    std::vector<std::string> warnings;
};

/// Load a graph from `<dir>/nodes.csv` and `<dir>/edges.csv`.
///
/// nodes.csv: `id,x,y`   (Geographic space reads these as `id,lon,lat`)
/// edges.csv: `u,v,w`
///
/// Every field is parsed without throwing; a malformed row yields an Error
/// naming the file and line, or a warning when `lenient` is set.
Result<LoadReport> load_csv_dir(const std::string& dir, const LoadOptions& opts = {});

/// Load from explicit paths.
Result<LoadReport> load_csv(const std::string& nodes_path, const std::string& edges_path,
                            const LoadOptions& opts = {});

}  // namespace agss
