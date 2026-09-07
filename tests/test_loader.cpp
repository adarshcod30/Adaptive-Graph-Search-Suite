// Regression tests for the input-validation failures found in the original
// engine: two aborts and one silent wrong answer.
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "agss/algorithm.hpp"
#include "agss/loader.hpp"
#include "microtest.hpp"

using namespace agss;

namespace {

/// Scratch directory that cleans itself up.
///
/// Uses std::filesystem rather than shelling out: the previous version called
/// `mkdir -p` and `rm -rf` through std::system, which is a syntax error on
/// Windows, so the directory never existed and the tests crashed rather than
/// failing with a message.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& tag) {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) / ("agss_test_" + tag);
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string dir() const { return path.string(); }

    void write(const std::string& name, const std::string& body) const {
        std::ofstream f(path / name, std::ios::binary);
        f << body;
    }
};

}  // namespace

TEST("loader", "malformed node id is an error, not an abort") {
    // Previously: std::stoi threw std::invalid_argument out of main -> SIGABRT.
    TempDir d("badnode");
    d.write("nodes.csv", "id,x,y\nabc,1,2\n");
    d.write("edges.csv", "u,v,w\n0,1,1\n");
    auto r = load_csv_dir(d.dir());
    CHECK(!r.ok());
    CHECK(r.error().line == 2);
    CHECK(r.error().message.find("unparseable") != std::string::npos);
}

TEST("loader", "malformed weight is an error") {
    TempDir d("badweight");
    d.write("nodes.csv", "id,x,y\n0,0,0\n1,1,1\n");
    d.write("edges.csv", "u,v,w\n0,1,not-a-number\n");
    auto r = load_csv_dir(d.dir());
    CHECK(!r.ok());
    CHECK(r.error().line == 2);
}

TEST("loader", "edge to an undeclared node is rejected") {
    // This one root cause previously produced three separate failures:
    // Greedy aborted, A* silently skipped, Floyd-Warshall invented a path.
    TempDir d("ghost");
    d.write("nodes.csv", "id,x,y\n0,0,0\n1,1,0\n2,50,50\n");
    d.write("edges.csv", "u,v,w\n0,1,1\n1,777,1\n");
    auto r = load_csv_dir(d.dir());
    CHECK(!r.ok());
    CHECK(r.error().message.find("777") != std::string::npos);
}

TEST("loader", "lenient mode skips the bad row and reports it") {
    TempDir d("lenient");
    d.write("nodes.csv", "id,x,y\n0,0,0\n1,1,0\n2,50,50\n");
    d.write("edges.csv", "u,v,w\n0,1,1\n1,777,1\n");
    LoadOptions o;
    o.lenient = true;
    auto r = load_csv_dir(d.dir(), o);
    CHECK(r.ok());
    CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    CHECK_EQ(r.value().graph.num_nodes(), 3);
    CHECK_EQ(r.value().graph.num_edges(), 1);
}

TEST("loader", "no algorithm invents a path to an unreachable node") {
    // Floyd-Warshall used to answer [0, 1, 777, 2] here and report success.
    TempDir d("unreachable");
    d.write("nodes.csv", "id,x,y\n0,0,0\n1,1,0\n2,50,50\n");
    d.write("edges.csv", "u,v,w\n0,1,1\n1,777,1\n");
    LoadOptions o;
    o.lenient = true;
    auto r = load_csv_dir(d.dir(), o);
    CHECK(r.ok());
    const auto& g = r.value().graph;
    for (const auto& k : Registry::instance().keys()) {
        const auto res = Registry::instance().create(k)->run(g, 0, 2, {});
        CHECK_MSG(!res.success, k << " claims to reach an unreachable node");
        CHECK_MSG(res.path.empty(), k << " returned a path to an unreachable node");
    }
}

TEST("loader", "negative weights are rejected unless asked for") {
    TempDir d("negative");
    d.write("nodes.csv", "id,x,y\n0,0,0\n1,1,1\n");
    d.write("edges.csv", "u,v,w\n0,1,-5\n");
    CHECK(!load_csv_dir(d.dir()).ok());
    LoadOptions o;
    o.allow_negative_weights = true;
    CHECK(load_csv_dir(d.dir(), o).ok());
}

TEST("loader", "missing files are reported, not crashed on") {
    auto r = load_csv_dir("/definitely/not/a/real/path");
    CHECK(!r.ok());
    CHECK(r.error().message.find("cannot open") != std::string::npos);
}

TEST("loader", "headerless files load") {
    TempDir d("noheader");
    d.write("nodes.csv", "0,0,0\n1,1,1\n");
    d.write("edges.csv", "0,1,2.5\n");
    auto r = load_csv_dir(d.dir());
    CHECK(r.ok());
    CHECK_EQ(r.value().graph.num_nodes(), 2);
    CHECK_EQ(r.value().graph.num_edges(), 1);
}
