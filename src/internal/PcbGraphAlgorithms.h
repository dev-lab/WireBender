/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "PcbGraph.h"
#include "core/bend/geometry.h"
#include "core/Debug.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace WireBender {

/**
 * @brief Detects and resolves one self-intersection in the given net graph.
 *
 * The function performs a single pass over all edge pairs looking for a
 * proper crossing.  When one is found it:
 *  1. Removes both crossing edges.
 *  2. Injects a new junction node at the geometric intersection point.
 *  3. Reconnects all four original endpoints to the new junction.
 *
 * Because only one intersection is repaired per call, the caller must invoke
 * this function repeatedly until it returns @c false to guarantee that no
 * crossings remain.
 *
 * @param netId    Zero-based ID of the net being processed (used for logging).
 * @param g        The routing graph for one net.  Modified in-place.
 * @param netName  Human-readable net name used in log messages.
 * @return         True if an intersection was found and repaired; false if the
 *                 graph is crossing-free.
 */
inline bool resolveSelfIntersections(int netId,
									 std::vector<GraphNode>& g,
									 const std::string& netName) {
	// Enumerate all active edges (each pair stored once with u < v).
	std::vector<Edge> edges;
	for(int i = 0; i < (int)g.size(); ++i) {
		if(!g[i].active) continue;
		for(int nxt: g[i].adj)
			if(i < nxt) edges.push_back({i, nxt});
	}

	for(size_t i = 0; i < edges.size(); ++i) {
		for(size_t j = i + 1; j < edges.size(); ++j) {
			int u1 = edges[i].u, v1 = edges[i].v;
			int u2 = edges[j].u, v2 = edges[j].v;

			// Skip adjacent edges — they share an endpoint and cannot cross.
			if(u1 == u2 || u1 == v2 || v1 == u2 || v1 == v2) continue;

			if(bend::segmentsIntersect(g[u1].p, g[v1].p, g[u2].p, g[v2].p)) {
				Point2D pX = bend::computeIntersection(g[u1].p, g[v1].p,
												 g[u2].p, g[v2].p);
				WB_LOG << "[PostProcessor] HINT: Self-intersection in "
					   << netName << " at (" << pX.x << ", " << pX.y
					   << "). Resolving via junction injection.\n";

				// Helper to remove a single directed adjacency.
				auto rem = [&](int a, int b) {
					g[a].adj.erase(
							std::remove(g[a].adj.begin(), g[a].adj.end(), b),
							g[a].adj.end());
					g[b].adj.erase(
							std::remove(g[b].adj.begin(), g[b].adj.end(), a),
							g[b].adj.end());
				};
				rem(u1, v1);
				rem(u2, v2);

				// Inject junction node connecting all four former endpoints.
				int newIdx = (int)g.size();
				g.push_back({pX, {u1, v1, u2, v2}, false, true});
				g[u1].adj.push_back(newIdx);
				g[v1].adj.push_back(newIdx);
				g[u2].adj.push_back(newIdx);
				g[v2].adj.push_back(newIdx);

				return true; // Signal: graph mutated, restart the outer loop.
			}
		}
	}
	return false;
}

/**
 * @brief Reduces the routing graph to a minimum spanning tree (MST).
 *
 * Cycles in a PCB routing graph represent redundant wire segments.  This
 * function eliminates them by running Kruskal's algorithm on the active
 * nodes, keeping only the shortest edges that maintain full connectivity.
 *
 * All existing adjacency lists are cleared before the MST edges are
 * written back, so the result is always a clean tree regardless of the
 * previous graph state.
 *
 * @param g  The routing graph for one net.  Modified in-place.
 */
inline void enforceTreeStructure(std::vector<GraphNode>& g) {
	// Collect all active edges with their squared lengths.
	std::vector<EdgeInfo> allEdges;
	for(int i = 0; i < (int)g.size(); ++i) {
		if(!g[i].active) continue;
		for(int nxt: g[i].adj) {
			if(i < nxt && g[nxt].active) {
				double dx = g[i].p.x - g[nxt].p.x;
				double dy = g[i].p.y - g[nxt].p.y;
				allEdges.push_back({i, nxt, dx * dx + dy * dy});
			}
		}
		g[i].adj.clear(); // Reset; MST edges are written back below.
	}

	// Sort edges by length (Kruskal's greedy criterion).
	std::sort(allEdges.begin(), allEdges.end(),
			  [](const EdgeInfo& a, const EdgeInfo& b) {
				  return a.len < b.len;
			  });

	// Union-Find for cycle detection.
	std::vector<int> parent(g.size());
	std::iota(parent.begin(), parent.end(), 0);
	auto find = [&](auto& self, int i) -> int {
		return (parent[i] == i) ? i : (parent[i] = self(self, parent[i]));
	};

	// Greedily add edges that connect two different components.
	for(const auto& e: allEdges) {
		int rootU = find(find, e.u);
		int rootV = find(find, e.v);
		if(rootU != rootV) {
			g[e.u].adj.push_back(e.v);
			g[e.v].adj.push_back(e.u);
			parent[rootU] = rootV;
		}
	}
}

} // namespace WireBender
