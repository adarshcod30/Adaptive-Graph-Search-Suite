"""Tests for the Python bindings.

These check the binding layer, not the algorithms -- those are covered by the
C++ suite. What can break here is ownership, argument conversion and lifetime:
a preprocessed index borrows its graph, and letting Python collect the graph
first would be a use-after-free with no C++ test able to see it.
"""
import gc
import math

import pytest

agss = pytest.importorskip("agss")


def tiny_graph():
    # 0 --5-- 1 --5-- 2, plus a 12-long direct edge that must lose.
    return agss.from_arrays(
        [(0, 0.0, 0.0), (1, 3.0, 4.0), (2, 6.0, 0.0)],
        [(0, 1, 5.0), (1, 0, 5.0), (1, 2, 5.0), (2, 1, 5.0), (0, 2, 12.0), (2, 0, 12.0)],
    )


def test_algorithms_are_listed():
    names = agss.algorithms()
    assert len(names) >= 10
    for expected in ("dijkstra", "astar", "ch", "alt", "bfs"):
        assert expected in names


def test_from_arrays_and_route():
    g = tiny_graph()
    assert g.num_nodes == 3
    assert g.num_edges == 6
    r = g.route("dijkstra", 0, 2)
    assert r.success
    assert r.cost == pytest.approx(10.0)
    assert r.path == [0, 1, 2]
    assert "SearchResult" in repr(r)


def test_every_optimal_algorithm_agrees():
    g = tiny_graph()
    costs = {name: g.route(name, 0, 2).cost
             for name in ("dijkstra", "astar", "bellmanford", "johnson", "bidijkstra")}
    assert all(c == pytest.approx(10.0) for c in costs.values()), costs


def test_unknown_algorithm_raises():
    g = tiny_graph()
    with pytest.raises(ValueError):
        g.route("no-such-algorithm", 0, 1)


def test_malformed_input_raises():
    with pytest.raises(ValueError):
        # An edge pointing at a node that was never declared.
        agss.from_arrays([(0, 0.0, 0.0)], [(0, 99, 1.0)])
    with pytest.raises(RuntimeError):
        agss.load("/definitely/not/a/real/directory")


def test_contraction_hierarchy_matches_dijkstra():
    g = tiny_graph()
    ch = agss.ContractionHierarchy()
    ch.build(g)
    assert ch.ready
    assert ch.build_ms >= 0
    q = ch.query(0, 2)
    assert q.success
    assert q.cost == pytest.approx(g.route("dijkstra", 0, 2).cost)


def test_alt_and_cch_match_dijkstra():
    g = tiny_graph()
    want = g.route("dijkstra", 0, 2).cost

    alt = agss.AltIndex()
    alt.build(g, landmarks=2)
    assert alt.query(0, 2).cost == pytest.approx(want)
    assert len(alt.landmarks) >= 1
    assert alt.lower_bound(0, 2) <= want + 1e-9

    cch = agss.CustomizableCH()
    cch.build(g)
    cch.customize()
    assert cch.ready
    assert cch.query(0, 2).cost == pytest.approx(want)


def test_cch_recustomizes_without_rebuilding():
    g = tiny_graph()
    cch = agss.CustomizableCH()
    cch.build(g)
    cch.customize()
    first = cch.query(0, 2).cost

    # Double every weight; the route should cost twice as much.
    doubled = [w * 2 for _, _, w in g.edge_list()]
    cch.customize(doubled)
    assert cch.query(0, 2).cost == pytest.approx(first * 2)


def test_index_keeps_its_graph_alive():
    # The C++ index holds a raw pointer to the graph. keep_alive must stop
    # Python collecting the graph out from under it.
    ch = agss.ContractionHierarchy()
    ch.build(tiny_graph())
    gc.collect()
    assert ch.query(0, 2).cost == pytest.approx(10.0)


def test_time_dependent_rush_hour():
    g = tiny_graph()
    m = agss.TimeDependentModel(g, free_flow_speed_mps=10.0)
    quiet = agss.earliest_arrival(m, 0, 2, 3 * 3600.0)
    peak = agss.earliest_arrival(m, 0, 2, 18 * 3600.0)
    assert quiet.search.success and peak.search.success
    assert peak.duration >= quiet.duration
    assert quiet.arrival == pytest.approx(quiet.departure + quiet.duration)

    scan = agss.scan_departures(m, 0, 2, samples=12)
    assert len(scan.departures) == 12
    assert scan.best_duration <= scan.worst_duration


def test_analysis_functions():
    g = tiny_graph()
    br, aps, comps = agss.bridges(g)
    assert comps == 1
    assert isinstance(br, list)

    scc = agss.strongly_connected_components(g)
    assert len(scc) == 3
    assert len(set(scc)) == 1  # fully bidirectional, so one component

    edges, weight = agss.minimum_spanning_tree(g)
    assert len(edges) == 2
    assert weight == pytest.approx(10.0)

    flow, cut = agss.max_flow(g, 0, 2)
    assert flow > 0

    cent = agss.betweenness_centrality(g)
    assert len(cent) == 3
    assert cent[1] > cent[0]  # the middle node carries the traffic


def test_k_shortest_and_isochrone_and_directions():
    g = tiny_graph()
    routes = agss.k_shortest_paths(g, 0, 2, k=2)
    assert len(routes) == 2
    assert routes[0][1] <= routes[1][1]

    bands, cost_to = agss.isochrone(g, 0, [6.0, 20.0])
    assert len(bands) == 2
    assert len(bands[0][1]) <= len(bands[1][1])
    assert math.isclose(cost_to[0], 0.0)

    steps, total = agss.directions(g, [0, 1, 2])
    assert len(steps) >= 2
    assert total > 0


def test_numpy_coordinates_and_spatial_index():
    np = pytest.importorskip("numpy")
    g = tiny_graph()
    xs, ys = g.coordinates()
    assert isinstance(xs, np.ndarray)
    assert xs.shape == (3,)
    assert g.nearest(0.1, 0.1) == 0
    assert g.nearest(5.9, 0.1) == 2
