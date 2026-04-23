#pragma once

#include "ConnectorEnd.h"
#include "Junction.h"
#include "orthogonal.h"
#include <vector>

namespace bend {
/**
 * Junction with its connector ends (edges).
 */
class JunctionEdges {
public:

	JunctionEdges() noexcept = default;

	inline explicit JunctionEdges(const Junction& junction, const std::vector<ConnectorEnd>& connectorEnds) noexcept
		: junction(junction), connectorEnds(connectorEnds) {
	}

	inline void setJunction(const Junction& j) {
		if(j.getId() == junction.getId()) return;
		clearCache();
		junction = j;
	}

	inline void setConnectorEnds(const std::vector<ConnectorEnd>& ends) {
		clearCache();
		connectorEnds = ends;
	}

	inline void addConnectorEnd(const ConnectorEnd& end) {
		for(const ConnectorEnd& c: connectorEnds) {
			// can't connect with both ends the same junction
			if(c.getId() == end.getId()) return;
		}
		clearCache();
		connectorEnds.push_back(end);
	}

	inline bool isLockX() const {
		initLocks();
		return lockX;
	}

	inline bool isLockY() const {
		initLocks();
		return lockY;
	}

	inline const Junction& getJunction() const {
		return junction;
	}

	inline const std::vector<ConnectorEnd>& getConnectorEnds() const {
		return connectorEnds;
	}

	inline size_t size() const {
		return getConnectorEnds().size();
	}
	/**
	 * Compute the intersection centroid of all connector ends attached to
	 * this junction. Each connector end contributes its near-end edge
	 * (the segment closest to the junction), which carries both the
	 * orientation and the coordinate needed for intersection calculation.
	 *
	 * @return best representative point for junction placement.
	 *		   Falls back to mean of edge midpoints when all edges share
	 *		   the same orientation (see computeIntersectionCentroid).
	 */
	inline const Avoid::Point& getCentroid() const {
		initCentroid();
		return centroid;
	}

	/**
	 * @brief Collect anchor coordinates from non-collinear ends along free axis.
	 * These are the X (or Y) values where a wire already runs - ideal
	 * junction positions that avoid adding extra bends.
	 * @param center results a sorted by distance from center
	 */
	std::vector<double> getJunctionAnchors1d(const Avoid::Point& center) const {
		std::vector<double> anchors;
		if(isLockX() == isLockY()) {
			// either 2D or 0D
			return anchors;
		}
		for(const ConnectorEnd& ce: getConnectorEnds()) {
			if(ce.isOrthogonalCollinear()) continue; // collinear ends define the axis, not anchors
			const Avoid::Edge edge = ce.getEndEdge();
			// edge.b is one step into the wire away from the junction.
			double coord = isLockX() ? edge.b.x : edge.b.y;
			// Deduplicate.
			bool found = false;
			for(double a: anchors)
				if(std::abs(a - coord) < 0.5) {
					found = true;
					break;
				}
			if(!found) anchors.push_back(coord);
		}

		// Sort anchors by distance to snapped center along free axis.
		double centerCoord = isLockX() ? center.x : center.y;
		std::sort(anchors.begin(), anchors.end(), [&](double a, double b) {
			return std::abs(a - centerCoord) < std::abs(b - centerCoord);
		});
		return anchors;
	}

	/**
	 * @brief Collect anchor coordinates from non-collinear ends along free axis.
	 * These are the X (or Y) values where a wire already runs - ideal
	 * junction positions that avoid adding extra bends.
	 */
	inline std::vector<double> getJunctionAnchors1d() const {
		return getJunctionAnchors1d(getCentroid());
	}
private:
	inline void clearCache() const {
		locksCalculated = false;
		centroidCalculated = false;
	}

	inline void initLocks() const {
		if(locksCalculated) return;
		for(const ConnectorEnd& ce: connectorEnds) {
			if(ce.isOrthogonalCollinear()) {
				if(ce.isOrthogonalY()) {
					lockX = true;
				} else {
					lockY = true;
				}
			}
		}
		locksCalculated = true;
	}

	inline void initCentroid() const {
		if(centroidCalculated) return;
		std::vector<Avoid::Edge> edges;
		edges.reserve(getConnectorEnds().size());
		for(const ConnectorEnd& ce: getConnectorEnds()) {
			edges.push_back(ce.getEdge());
		}
		centroid = bestOrthogonalIntersection(edges);
		centroidCalculated = true;
	}

	Junction junction;
	std::vector<ConnectorEnd> connectorEnds;
	mutable bool locksCalculated = false;
	mutable bool lockX = false; // horizontal collinear end: Y fixed, sweep X
	mutable bool lockY = false; // vertical collinear end: X fixed, sweep Y
	mutable bool centroidCalculated = false;
	mutable Avoid::Point centroid;
};
} // namespace bend