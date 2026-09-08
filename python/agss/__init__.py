"""Adaptive Graph Search Suite: pathfinding and network analysis on road graphs.

The C++ engine, exposed for the people who actually analyse networks and who
are not going to link a C++ library to try an idea.

    import agss

    g = agss.load("data/cities/Jaipur", geographic=True)
    r = g.route("astar", 2746, 16278)
    print(r.cost, len(r.path))

    ch = agss.ContractionHierarchy()
    ch.build(g)                      # seconds, once
    fast = ch.query(2746, 16278)     # microseconds, thereafter
"""

from ._agss import (  # noqa: F401
    AltIndex,
    ContractionHierarchy,
    CustomizableCH,
    DepartureScan,
    Graph,
    SearchOptions,
    SearchResult,
    TimeDependentModel,
    TimeDependentResult,
    algorithms,
    betweenness_centrality,
    bridges,
    directions,
    earliest_arrival,
    from_arrays,
    isochrone,
    k_shortest_paths,
    load,
    max_flow,
    minimum_spanning_tree,
    scan_departures,
    strongly_connected_components,
)

__all__ = [
    "AltIndex",
    "ContractionHierarchy",
    "CustomizableCH",
    "DepartureScan",
    "Graph",
    "SearchOptions",
    "SearchResult",
    "TimeDependentModel",
    "TimeDependentResult",
    "algorithms",
    "betweenness_centrality",
    "bridges",
    "directions",
    "earliest_arrival",
    "from_arrays",
    "isochrone",
    "k_shortest_paths",
    "load",
    "max_flow",
    "minimum_spanning_tree",
    "scan_departures",
    "strongly_connected_components",
]

__version__ = "0.3.0"
