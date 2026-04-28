/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "core/bend/geometry.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WireBender {

/**
 * @brief A node in a per-net routing graph.
 *
 * Each node represents either a component pad or an intermediate routing
 * point (a bend vertex or a Steiner junction injected during post-processing).
 * Nodes can be deactivated in-place rather than erased; consumers must check
 * the @c active flag before using a node.
 */
struct GraphNode {
	/** @brief 2-D position of this node. */
	Point2D p;

	/**
	 * @brief Indices into the owning graph vector of all adjacent nodes.
	 *
	 * The graph is undirected: if j ∈ adj[i] then i ∈ adj[j].
	 */
	std::vector<int> adj;

	/** @brief True if this node sits on a component pad. */
	bool isPad;

	/**
	 * @brief Whether this node is still part of the live graph.
	 *
	 * Nodes are logically removed by setting this to false rather than
	 * physically erasing them, so that previously recorded indices remain
	 * stable during iterative refinement passes.
	 */
	bool active = true;
};

/**
 * @brief A directed segment belonging to a specific net, used for global
 *        clearance checks between different nets.
 */
struct GlobalSegment {
	/** @brief Zero-based index of the net that owns this segment. */
	int netId;

	/** @brief Start point of the segment. */
	Point2D p1;

	/** @brief End point of the segment. */
	Point2D p2;
};

/**
 * @brief An undirected edge between two graph nodes, identified by index.
 *
 * Used internally by @c resolveSelfIntersections to enumerate all edges
 * before looking for crossing pairs.
 */
struct Edge {
	/** @brief Index of the first endpoint node. */
	int u;
	/** @brief Index of the second endpoint node. */
	int v;
};

/**
 * @brief A weighted undirected edge, used by the Kruskal MST step.
 *
 * @see enforceTreeStructure
 */
struct EdgeInfo {
	/** @brief Index of the first endpoint node. */
	int u;
	/** @brief Index of the second endpoint node. */
	int v;
	/** @brief Squared Euclidean length of the edge (weight for MST). */
	double len;
};

// ---------------------------------------------------------------------------
// Graph-building helpers
// ---------------------------------------------------------------------------

/**
 * @brief Builds per-net routing graphs from a flat list of wires.
 *
 * For each wire in @p wires every consecutive point pair becomes a graph
 * edge.  Duplicate nodes (within 1e-4 units) are merged.  The function also
 * fills @p allSegs with all segments across all nets for later inter-net
 * clearance checks, and populates the @p netNameToId / @p netNames
 * lookup tables.
 *
 * @param wires       Input wires from the router.
 * @param nets        Full net list (used to identify pad locations).
 * @param[out] netGraphs    One graph per net, in the same order as @p netNames.
 * @param[out] allSegs      Flat list of all segments with their net IDs.
 * @param[out] netNames     Net names ordered by their assigned ID.
 * @param[out] netNameToId  Map from net name to integer ID.
 */
inline void buildNetGraphs(
		const std::vector<Wire>& wires,
		const std::vector<PcbNet>& nets,
		std::vector<std::vector<GraphNode>>& netGraphs,
		std::vector<GlobalSegment>& allSegs,
		std::vector<std::string>& netNames,
		std::unordered_map<std::string, int>& netNameToId) {
	// --- Assign integer IDs to net names, collect global segments ----------
	int nId = 0;
	for(const auto& w: wires) {
		if(netNameToId.find(w.net) == netNameToId.end()) {
			netNameToId[w.net] = nId++;
			netNames.push_back(w.net);
		}
		int id = netNameToId[w.net];
		for(size_t i = 0; i + 1 < w.points.size(); ++i)
			allSegs.push_back({id, w.points[i], w.points[i + 1]});
	}

	// --- Helper: is this point coincident with any component pad? ----------
	auto isPad = [&](const Point2D& p) {
		for(const auto& n: nets)
			for(const auto& pad: n.pads)
				if(bend::pointToSegmentDistSq(p, pad, pad) < 1e-4) return true;
		return false;
	};

	// --- Build per-net graphs ----------------------------------------------
	netGraphs.resize(netNames.size());

	for(const auto& w: wires) {
		int id = netNameToId[w.net];
		auto& g = netGraphs[id];

		/**
		 * Returns the index of an existing node at position @p p (within
		 * 1e-4 units), or appends a new node and returns its index.
		 */
		auto addNode = [&](Point2D p) -> int {
			for(int i = 0; i < (int)g.size(); ++i)
				if(g[i].active && bend::pointToSegmentDistSq(g[i].p, p, p) < 1e-4)
					return i;
			g.push_back({p, {}, isPad(p), true});
			return (int)g.size() - 1;
		};

		for(size_t i = 0; i + 1 < w.points.size(); ++i) {
			int u = addNode(w.points[i]);
			int v = addNode(w.points[i + 1]);
			if(u == v) continue;
			if(std::find(g[u].adj.begin(), g[u].adj.end(), v) == g[u].adj.end()) {
				g[u].adj.push_back(v);
				g[v].adj.push_back(u);
			}
		}
	}
}

} // namespace WireBender
