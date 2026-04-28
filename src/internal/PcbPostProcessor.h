/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "PcbGraph.h"
#include "PcbGraphAlgorithms.h"
#include "core/Debug.h"
#include "core/bend/geometry.h"
#include "core/bend/utils.h"
#include "libavoid/libavoid.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace WireBender {

/**
 * @brief Post-processes a libavoid PCB routing result to improve wire quality.
 *
 * After the router produces an initial solution this class applies a series
 * of iterative graph-based refinements to each net:
 *
 *  1. Self-intersection resolution - crossing wire segments within the
 *     same net are detected and resolved by injecting a junction node at the
 *     crossing point (@see resolveSelfIntersections).
 *
 *  2. Sharp-corner relaxation / Steiner-junction injection - obtuse
 *     angles that exceed a configurable threshold are smoothed.  For simple
 *     degree-2 bend nodes the corner is replaced by a short diagonal chamfer;
 *     for higher-degree junctions a Steiner point is nudged outward along the
 *     bisector direction.  Both operations respect the minimum clearance to
 *     all other-net segments.
 *
 *  3. Cycle elimination - after the iterative refinement loop the graph
 *     is reduced to a minimum spanning tree to remove any redundant segments
 *     introduced during routing (@see enforceTreeStructure).
 *
 * The result is serialised back into the @c PcbRouteResult wire/junction
 * format expected by the rest of the pipeline.
 *
 * @note The class holds a reference to the net list passed at construction.
 *       The caller must ensure that the net list outlives the processor.
 */
class PcbPostProcessor {
public:
	/**
	 * @brief Constructs a post-processor for the given set of nets.
	 *
	 * @param nets           All nets in the design.  Referenced but not copied.
	 * @param clearance      Minimum clearance (in board units) that must be
	 *                       maintained between segments belonging to different
	 *                       nets.  Defaults to 4.0.
	 * @param sharpAngleDeg  Interior angle (in degrees) below which a corner
	 *                       is considered "sharp" and will be relaxed.
	 *                       Defaults to 110°.
	 */
	PcbPostProcessor(const std::vector<PcbNet>& nets,
					 double clearance = 4.0,
					 double sharpAngleDeg = 110.0)
		: nets(nets),
		  minClearanceSq(clearance * clearance),
		  cosThreshold(std::cos(sharpAngleDeg * M_PI / 180.0)) {}

	/**
	 * @brief Runs all post-processing passes and returns the refined result.
	 *
	 * The input is not modified.  Each pass iterates to convergence (or up
	 * to an internal iteration cap) before the next pass is applied.
	 *
	 * @param input  The raw routing result produced by the router.
	 * @return       A new @c PcbRouteResult with improved wire geometry.
	 */
	PcbRouteResult process(const PcbRouteResult& input) {
		WB_LOG << "[PostProcessor] Starting processing. Input wires: "
			   << input.wires.size() << "\n";

		// --- Build per-net graphs and global segment list ------------------
		std::vector<std::vector<GraphNode>> netGraphs;
		std::vector<GlobalSegment> allSegs;
		std::vector<std::string> netNames;
		std::unordered_map<std::string, int> netNameToId;

		buildNetGraphs(input.wires, nets,
					   netGraphs, allSegs, netNames, netNameToId);

		// --- Iterative refinement per net ----------------------------------
		for(int netId = 0; netId < (int)netGraphs.size(); ++netId) {
			auto& g = netGraphs[netId];
			bool changed = true;
			int iterations = 0;

			while(changed && iterations++ < 15) {
				changed = false;

				// Pass 1: resolve any remaining self-intersections first.
				if(resolveSelfIntersections(netId, g, netNames[netId])) {
					changed = true;
					continue;
				}

				// Pass 2: relax sharp corners and inject Steiner junctions.
				changed = relaxSharpCorners(netId, g, allSegs);
			}

			// Final cleanup: ensure the net graph is a minimal spanning tree.
			enforceTreeStructure(g);
		}

		// Serialise graphs back to wire/junction lists
		return serialiseGraphs(netGraphs, netNames);
	}

private:

	/**
	 * @brief Performs one full sharp-corner relaxation pass over the graph.
	 *
	 * Iterates over every node with degree ≥ 2 and inspects each pair of
	 * incident edges.  If the angle between the two edges is sharper than
	 * @c cosThreshold_, either a chamfer (for degree-2 non-pad nodes) or a
	 * Steiner point displacement (for junction nodes) is attempted.  The
	 * operation is only committed if the resulting new segments maintain at
	 * least @c minClearanceSq_ clearance from all other-net segments.
	 *
	 * @param netId    Zero-based ID of the net being processed (for logging).
	 * @param g        The per-net routing graph.  Modified in-place.
	 * @param allSegs  All segments across all nets, for clearance checks.
	 * @return         True if at least one modification was made to the graph.
	 */
	bool relaxSharpCorners(int netId,
						   std::vector<GraphNode>& g,
						   const std::vector<GlobalSegment>& allSegs) {
		for(int i = 0; i < (int)g.size(); ++i) {
			if(!g[i].active || g[i].adj.size() < 2) continue;

			for(size_t idxU = 0; idxU < g[i].adj.size(); ++idxU) {
				for(size_t idxV = idxU + 1; idxV < g[i].adj.size(); ++idxV) {
					int u = g[i].adj[idxU];
					int v = g[i].adj[idxV];

					Point2D p = g[i].p;
					Point2D pu = g[u].p;
					Point2D pv = g[v].p;

					Point2D vec1 = {pu.x - p.x, pu.y - p.y};
					Point2D vec2 = {pv.x - p.x, pv.y - p.y};
					double l1 = std::hypot(vec1.x, vec1.y);
					double l2 = std::hypot(vec2.x, vec2.y);
					if(l1 < 1e-3 || l2 < 1e-3) continue;

					double cosAngle = (vec1.x * vec2.x + vec1.y * vec2.y)
									  / (l1 * l2);
					if(cosAngle <= cosThreshold) continue;

					double L = std::min(l1, l2) * 0.4;

					if(!g[i].isPad && g[i].adj.size() == 2) {
						// Degree-2 non-pad bend: replace with a chamfer edge.
						if(tryChamferCorner(netId, i, u, v,
											vec1, vec2, l1, l2, L,
											g, allSegs))
							return true;
					} else {
						// Junction or pad: nudge a Steiner point outward.
						if(tryInjectSteinerJunction(netId, i, u, v,
													p, pu, pv,
													vec1, vec2, l1, l2, L,
													g, allSegs))
							return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * @brief Attempts to replace a degree-2 bend node with a chamfer segment.
	 *
	 * Two new nodes D and E are placed at distance @p L along the outgoing
	 * edge directions from the bend node @p i.  The bend node is deactivated
	 * and replaced by the edge D–E.  The operation is aborted if the new edge
	 * would violate the minimum clearance to any other-net segment.
	 *
	 * @param netId   Net ID for logging.
	 * @param i       Index of the bend node.
	 * @param u       Index of the first neighbour.
	 * @param v       Index of the second neighbour.
	 * @param vec1    Direction vector from i toward u (unnormalised).
	 * @param vec2    Direction vector from i toward v (unnormalised).
	 * @param l1      Length of vec1.
	 * @param l2      Length of vec2.
	 * @param L       Chamfer offset distance (fraction of the shorter edge).
	 * @param g       Graph to modify.
	 * @param allSegs Global segments for clearance checks.
	 * @return        True if the chamfer was successfully applied.
	 */
	bool tryChamferCorner(int netId,
						  int i, int u, int v,
						  const Point2D& vec1,
						  const Point2D& vec2,
						  double l1, double l2, double L,
						  std::vector<GraphNode>& g,
						  const std::vector<GlobalSegment>& allSegs) {
		Point2D p = g[i].p;
		Point2D D = {p.x + (vec1.x / l1) * L, p.y + (vec1.y / l1) * L};
		Point2D E = {p.x + (vec2.x / l2) * L, p.y + (vec2.y / l2) * L};

		// Verify clearance of the chamfer edge against all other-net segments.
		for(const auto& seg: allSegs) {
			if(seg.netId != netId && bend::segmentToSegmentDistSq(D, E, seg.p1, seg.p2) < minClearanceSq)
				return false;
		}

		WB_LOG << "[PostProcessor] Relaxing corner at ("
			   << p.x << ", " << p.y << ")\n";

		// Detach the old bend node.
		auto erase = [&](std::vector<int>& vec, int val) {
			vec.erase(std::remove(vec.begin(), vec.end(), val), vec.end());
		};
		erase(g[u].adj, i);
		erase(g[v].adj, i);
		g[i].active = false;

		// Insert chamfer nodes D and E.
		int idD = (int)g.size();
		g.push_back({D, {u}, false, true});
		int idE = (int)g.size();
		g.push_back({E, {v, idD}, false, true});

		g[u].adj.push_back(idD);
		g[v].adj.push_back(idE);
		g[idD].adj.push_back(idE);

		return true;
	}

	/**
	 * @brief Attempts to displace a junction node outward along the bisector.
	 *
	 * A new Steiner point J is computed at distance @p L along the normalised
	 * sum of the two incident edge directions.  The original node @p i is
	 * detached from neighbours @p u and @p v (but remains connected to any
	 * remaining neighbours), and all three — i, u, v — are re-connected to J.
	 * The operation is aborted if any of the three new sub-segments would
	 * violate the minimum clearance.
	 *
	 * @param netId   Net ID for logging.
	 * @param i       Index of the junction node.
	 * @param u       Index of the first neighbour involved in the sharp angle.
	 * @param v       Index of the second neighbour involved in the sharp angle.
	 * @param p       Position of node i.
	 * @param pu      Position of node u.
	 * @param pv      Position of node v.
	 * @param vec1    Direction vector from i toward u (unnormalised).
	 * @param vec2    Direction vector from i toward v (unnormalised).
	 * @param l1      Length of vec1.
	 * @param l2      Length of vec2.
	 * @param L       Displacement distance for J.
	 * @param g       Graph to modify.
	 * @param allSegs Global segments for clearance checks.
	 * @return        True if the Steiner point was successfully injected.
	 */
	bool tryInjectSteinerJunction(int netId,
								  int i, int u, int v,
								  const Point2D& p,
								  const Point2D& pu,
								  const Point2D& pv,
								  const Point2D& vec1,
								  const Point2D& vec2,
								  double l1, double l2, double L,
								  std::vector<GraphNode>& g,
								  const std::vector<GlobalSegment>& allSegs) {
		Point2D dir = {vec1.x / l1 + vec2.x / l2,
					   vec1.y / l1 + vec2.y / l2};
		double dlen = std::hypot(dir.x, dir.y);
		if(dlen < 1e-3) return false;

		Point2D J = {p.x + (dir.x / dlen) * L,
					 p.y + (dir.y / dlen) * L};

		// All three new sub-segments must respect clearance.
		for(const auto& seg: allSegs) {
			if(seg.netId == netId) continue;
			if(bend::segmentToSegmentDistSq(J, p, seg.p1, seg.p2) < minClearanceSq
			   || bend::segmentToSegmentDistSq(J, pu, seg.p1, seg.p2) < minClearanceSq
			   || bend::segmentToSegmentDistSq(J, pv, seg.p1, seg.p2) < minClearanceSq) {
				return false;
			}
		}

		WB_LOG << "[PostProcessor] Injecting Steiner junction near ("
			   << p.x << ", " << p.y << ")\n";

		auto erase = [&](std::vector<int>& vec, int val) {
			vec.erase(std::remove(vec.begin(), vec.end(), val), vec.end());
		};
		erase(g[i].adj, u);
		erase(g[i].adj, v);
		erase(g[u].adj, i);
		erase(g[v].adj, i);

		int idJ = (int)g.size();
		g.push_back({J, {i, u, v}, false, true});
		g[i].adj.push_back(idJ);
		g[u].adj.push_back(idJ);
		g[v].adj.push_back(idJ);

		return true;
	}

	/**
	 * @brief Converts the refined per-net graphs back into a @c PcbRouteResult.
	 *
	 * Traverses each graph and emits one wire per maximal chain of degree-2
	 * nodes, avoiding duplicate edges via a visited-edge map.  Nodes with
	 * degree ≥ 3 are also recorded as junctions.
	 *
	 * @param netGraphs  Refined per-net graphs.
	 * @param netNames   Net name for each graph index.
	 * @return           Serialised @c PcbRouteResult.
	 */
	PcbRouteResult serialiseGraphs(
			const std::vector<std::vector<GraphNode>>& netGraphs,
			const std::vector<std::string>& netNames) const {
		PcbRouteResult finalResult;

		for(int netId = 0; netId < (int)netGraphs.size(); ++netId) {
			const auto& g = netGraphs[netId];
			std::string netName = netNames[netId];

			std::unordered_map<std::pair<int, int>, bool, bend::PairHash> edgeVisited;
			auto getEdge = [](int a, int b) {
				return std::make_pair(std::min(a, b), std::max(a, b));
			};

			for(int i = 0; i < (int)g.size(); ++i) {
				if(!g[i].active) continue;

				// Record T-junctions and higher-degree nodes.
				if(g[i].adj.size() >= 3)
					finalResult.junctions.push_back({netName, g[i].p});

				// Emit wires starting from endpoints (degree 1) and junctions.
				if(g[i].adj.size() == 1 || g[i].adj.size() >= 3) {
					for(int nxt: g[i].adj) {
						if(!g[nxt].active) continue;
						if(edgeVisited[getEdge(i, nxt)]) continue;

						// Walk the chain of degree-2 nodes.
						std::vector<Point2D> pts = {g[i].p, g[nxt].p};
						int prev = i, curr = nxt;
						edgeVisited[getEdge(prev, curr)] = true;

						while(g[curr].adj.size() == 2) {
							int nextNode = (g[curr].adj[0] == prev)
												   ? g[curr].adj[1]
												   : g[curr].adj[0];
							if(!g[nextNode].active) break;
							if(edgeVisited[getEdge(curr, nextNode)]) break;
							prev = curr;
							curr = nextNode;
							edgeVisited[getEdge(prev, curr)] = true;
							pts.push_back(g[curr].p);
						}
						finalResult.wires.push_back({netName, pts});
					}
				}
			}
		}

		WB_LOG << "[PostProcessor] Finished processing.\n";
		return finalResult;
	}

	/**
	 * @brief All nets in the design (pad locations for pad-identification).
	 */
	const std::vector<PcbNet>& nets;

	/**
	 * @brief Squared minimum clearance between segments of different nets.
	 *
	 * Stored squared so that clearance checks avoid a square-root operation.
	 */
	double minClearanceSq;

	/**
	 * @brief Cosine of the sharp-angle threshold.
	 *
	 * A corner is considered sharp when the cosine of the interior angle
	 * exceeds this value (i.e. the angle is smaller than the threshold).
	 */
	double cosThreshold;
};

} // namespace WireBender
