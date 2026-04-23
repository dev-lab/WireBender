#pragma once

#include "NetRegistry.h"
#include "RouterShapePin.h"
#include "bend/Connector.h"
#include "bend/JunctionEdges.h"
#include "bend/JunctionPointResult.h"
#include "bend/OccupiedArea.h"
#include "bend/orthogonal.h"
#include "bend/utils.h"
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

class RouterState {
public:
	RouterState(const NetRegistry& netRegistry): netRegistry(netRegistry) {
		auto addJunction = [&](const bend::Junction& j, const bend::Connector& c, bool front, std::string net) {
			if(!j) return;
			netRegistry.cacheIdNet(j.getId(), net);
			auto& jp = junctionEdges[j.getId()];
			jp.setJunction(j);
			jp.addConnectorEnd(bend::ConnectorEnd(c, front));
		};
		for(Avoid::ConnRef* conn: netRegistry.getConnections()) {
			bend::Connector connector(conn);
			const auto& r = connector->displayRoute();
			std::string net = netRegistry.getNet(connector);
			connectors[connector.getId()] = connector;
			if(r.size() < 1) continue;
			auto junctions = connector.junctions();
			addJunction(junctions.first, connector, true, net);
			addJunction(junctions.second, connector, false, net);
		}
	}

	explicit RouterState(const RouterState& other) noexcept
		: netRegistry(other.netRegistry), connectors(other.connectors), junctionEdges(other.junctionEdges) {
	}

	explicit RouterState(RouterState&& other) noexcept
		: netRegistry(other.netRegistry), connectors(std::move(other.connectors)), junctionEdges(std::move(other.junctionEdges)) {
	}

	RouterState& operator=(const RouterState& other) = delete;
	RouterState& operator=(RouterState&& other) = delete;

	/**
	 * @return Net Registry
	 */
	inline const NetRegistry& getNetRegistry() const {
		return netRegistry;
	}

	/**
	 * @return connector IDs -> Connectors
	 */
	inline const std::map<unsigned int, bend::Connector>& getConnectors() const {
		return connectors;
	}

	/**
	 * @return junction IDs -> Junction Edges
	 */
	inline const std::map<unsigned int, bend::JunctionEdges>& getJunctionEdges() const {
		return junctionEdges;
	}

	/**
	 * Get net by libavoid object ID
	 * @param id libavoid object ID
	 * @return net name if found, otherwise empty string
	 */
	inline std::string getNetById(unsigned int id) const {
		return netRegistry.getNetById(id);
	}

	std::vector<bend::JunctionEdges*> getJunctionsToFix() {
		std::vector<bend::JunctionEdges*> result;
		for(auto& [id, jp]: junctionEdges) {
			if(jp.size() < 3) continue;
			for(auto& e: jp.getConnectorEnds()) {
				if(!e.isEndPointSame()) {
					result.push_back(&jp);
					break;
				}
			}
		}
		return result;
	}

	auto makeIdNetChecker(const std::string& netName) const {
		return [this, netName](unsigned int id) {
			return netRegistry.getNetById(id) == netName;
		};
	}

	/**
	 * Build the list of occupied rectangles around a junction to be moved.
	 *
	 * Excludes all connectors and junctions belonging to the same net.
	 * Segment obstacles carry orientation (H/V) so corridor checks can
	 * ignore perpendicular crossings.
	 */
	void buildOccupiedArea(const bend::JunctionEdges& jp, const NetRegistry& netReg, bend::OccupiedArea& occupied, bool trace = false) const {
		const std::string ownNet = netRegistry.getNetById(jp.getJunction().getId());
		occupied.setIsSameNet(makeIdNetChecker(ownNet));

		if(trace) {
			WB_LOG << "\t\tbuildOccupiedArea: center=" << bend::toStringPoint(occupied.getProxima().getCenter())
				   << " radius=" << bend::iround(occupied.getProxima().getRadius())
				   << " margin=" << bend::iround(occupied.getMargin())
				   << " ownNet=" << ownNet << "\n";
		}

		for(const Avoid::Obstacle* obs: netReg.getObstacles()) {
			if(occupied.add(obs)) {
				if(trace) WB_LOG << "\t\t  + obstacle " << occupied.getRects().back().toString() << "\n";
			}
		}
		if(trace) {
			WB_LOG << "\t\t	 shapes: " << occupied.getShapeCount() << " collected, "
				   << occupied.getShapeSkipped() << " out of radius\n";
			WB_LOG << "\t\t	 foreign junctions: " << occupied.getJunctionCount() << " collected, "
				   << occupied.getJunctionSameNet() << " same-net skipped, "
				   << occupied.getJunctionSkipped() << " out of radius\n";
		}

		// 3. Foreign connector segment obstacles (oriented)
		// Each segment is expanded only perpendicular to its axis.
		// Orientation is stored so corridor checks can ignore crossings.
		for(const auto& [id, connector]: connectors) {
			if(occupied.add(connector)) {
				if(trace) WB_LOG << "\t\t  + obstacle " << occupied.getRects().back().toString() << "\n";
			}
		}
		if(trace) {
			WB_LOG << "\t\t	 foreign segments: " << occupied.getSegmentCount() << " collected, "
				   << occupied.getSegmentSameNet() << " connectors same-net skipped, "
				   << occupied.getSegmentSkipped() << " segments out of radius\n";
			WB_LOG << "\t\t	 total obstacles: " << occupied.getRects().size() << "\n";
		}
		occupied.clearIsSameNet();
	}

	/**
	 * Find the best new position for a junction.
	 * Strategy (for 1-D case, free axis = X as example):
	 * 1) Anchor candidates: try the X coordinates of non-collinear connector ends first
	 * (these are positions where a wire already runs, so no extra bend is needed).
	 * 2) Grid sweep: if no anchor passes, fall back to the regular grid sweep outward
	 * from center.
	 * For each point, verify that every other connector end has a clear corridor to reach
	 * the candidate point from its approach direction.
	 */
	bend::JunctionPointResult findBestJunctionPoint(
			const bend::JunctionEdges& jp,
			const bend::OccupiedArea& occupied,
			double gridStep,
			bool trace = false) const {
		if(trace) {
			WB_LOG << "\t\tfindBestJunctionPoint: center=" << bend::toStringPoint(occupied.getProxima().getCenter())
				   << " radius=" << bend::iround(occupied.getProxima().getRadius())
				   << " gridStep=" << gridStep
				   << " lockX=" << jp.isLockX() << " lockY=" << jp.isLockY()
				   << " obstacles=" << occupied.getRects().size() << "\n";
		}

		if(jp.isLockX() && jp.isLockY()) {
			if(trace) WB_LOG << "\t\t  => both axes locked, returning recommendedPosition\n";
			return {jp.getJunction()->recommendedPosition(), true};
		}

		const Avoid::Point& center = occupied.getProxima().getCenter();
		const int maxSteps = static_cast<int>(
									 std::ceil(occupied.getProxima().getRadius() / (gridStep > 0.0 ? gridStep : 1.0)))
							 + 1;

		if(trace) {
			WB_LOG << "\t\t	  maxSteps=" << maxSteps << "\n";
			const bend::OccupiedRect* blocker = occupied.getBlockedBy(center);
			if(blocker) WB_LOG << "\t\t	 center blocked by " << blocker->toString() << "\n";
			else WB_LOG << "\t\t  center is FREE\n";
		}

		Avoid::Point bestFallback = center;
		double bestFallbackDist = std::numeric_limits<double>::max();
		int triedCount = 0;

		auto trackFallback = [&](const Avoid::Point& p) {
			double d = bend::distance2(p, center);
			if(d < bestFallbackDist) {
				bestFallbackDist = d;
				bestFallback = p;
			}
		};

		/**
		 * Corridor check for a candidate junction point.
		 * For each connector end, verify it can reach candPoint from its
		 * approach direction without overlapping a parallel wire.
		 * Perpendicular crossings are allowed (handled by blocksVertical /
		 * blocksHorizontal).
		 */
		auto corridorClear = [&](const Avoid::Point& cand) -> bool {
			for(const bend::ConnectorEnd& ce: jp.getConnectorEnds()) {
				const Avoid::Edge edge = ce.getEndEdge();
				const bend::OccupiedRect* r = occupied.getBlockedBy(cand, edge);
				if(nullptr != r) {
					if(trace) {
						WB_LOG << "\t\t	   corridor " << (bend::isOrthogonalY(edge) ? "H" : "V") << " blocked by " << r->toString()
							   << " for end edge " << bend::toStringEdge(edge) << "\n";
					}
					return false;
				}
			}
			return true;
		};

		auto tryPoint = [&](const Avoid::Point& cand, bend::JunctionPointResult& out) -> bool {
			if(!occupied.getProxima().isNearEnough(cand)) return false;
			++triedCount;
			if(occupied.isFree(cand)) {
				if(trace) WB_LOG << "\t\t  point " << bend::toStringPoint(cand) << ": free, checking corridors\n";
				if(corridorClear(cand)) {
					if(trace) WB_LOG << "\t\t  => point accepted at " << bend::toStringPoint(cand) << " after " << triedCount << " tried\n";
					out = {cand, false};
					return true;
				}
			} else {
				if(trace) WB_LOG << "\t\t  point " << bend::toStringPoint(cand) << ": point occupied, skipping\n";
			}
			trackFallback(cand);
			return false;
		};

		bend::JunctionPointResult out;

		// 1-D search
		if(jp.isLockX() || jp.isLockY()) {
			if(trace) WB_LOG << "\t\t  mode: 1-D search along "
							 << (jp.isLockX() ? "X" : "Y") << " axis\n";

			std::vector<double> anchors = jp.getJunctionAnchors1d(center);

			if(trace) {
				WB_LOG << "\t\t	 anchors:";
				for(double a: anchors) WB_LOG << " " << bend::iround(a);
				WB_LOG << "\n";
			}

			// Phase 1: try anchor positions with full corridor check.
			for(double anchor: anchors) {
				Avoid::Point cand(jp.isLockX() ? anchor : center.x, jp.isLockY() ? anchor : center.y);
				if(tryPoint(cand, out)) return out;
			}

			// Phase 2: grid sweep, point-free check only (no corridor check).
			if(trace) WB_LOG << "\t\t  falling back to grid sweep\n";
			for(int step = 0; step <= maxSteps; ++step) {
				double delta = step * gridStep;
				for(int sign: {0, -1, 1}) {
					if(step == 0 && sign != 0) continue;
					if(step != 0 && sign == 0) continue;
					double d = sign * delta;
					Avoid::Point cand(jp.isLockX() ? (center.x + d) : center.x, jp.isLockY() ? (center.y + d) : center.y);
					if(tryPoint(cand, out)) return out;
				}
			}

			if(trace) {
				WB_LOG << "\t\t	 => CLAMPED after " << triedCount
					   << " candidates, fallback=" << bend::toStringPoint(bestFallback) << "\n";
				WB_LOG << "\t\t	 obstacles on search line:\n";
				for(const bend::OccupiedRect& r: occupied.getRects()) {
					bool onLine = jp.isLockX() ? (r.minY <= center.y && center.y <= r.maxY)
											   : (r.minX <= center.x && center.x <= r.maxX);
					if(onLine) WB_LOG << "\t\t	  " << r.toString() << "\n";
				}
			}
			return {bestFallback, true};
		}

		// 2-D search
		if(trace) WB_LOG << "\t\t  mode: 2-D search from center\n";

		struct GridCandidate {
			double dist2;
			int ix, iy;
		};
		std::vector<GridCandidate> candidates;
		candidates.reserve(static_cast<size_t>(4 * maxSteps * maxSteps));

		double radius2 = occupied.getProxima().getRadius2();
		for(int ix = -maxSteps; ix <= maxSteps; ++ix) {
			for(int iy = -maxSteps; iy <= maxSteps; ++iy) {
				double dx = ix * gridStep, dy = iy * gridStep;
				double d2 = dx * dx + dy * dy;
				if(d2 <= radius2)
					candidates.push_back({d2, ix, iy});
			}
		}
		std::sort(candidates.begin(), candidates.end(),
				  [](const GridCandidate& a, const GridCandidate& b) {
					  return a.dist2 < b.dist2;
				  });

		for(const GridCandidate& c: candidates) {
			Avoid::Point cand(center.x + c.ix * gridStep, center.y + c.iy * gridStep);
			if(tryPoint(cand, out)) return out;
		}

		if(trace) WB_LOG << "\t\t  => CLAMPED after " << triedCount
						 << " candidates, fallback=" << bend::toStringPoint(bestFallback) << "\n";
		return {bestFallback, true};
	}

private:
	const NetRegistry& netRegistry;
	std::map<unsigned int, bend::Connector> connectors;
	std::map<unsigned int, bend::JunctionEdges> junctionEdges;
};
