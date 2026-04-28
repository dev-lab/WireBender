/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Connector.h"
#include "Proxima.h"
#include <functional>
#include <string>

namespace bend {

class OccupiedArea {
public:
	OccupiedArea(const Proxima& proxima, double margin): proxima(proxima), margin(margin) {
	}

	OccupiedArea(const Avoid::Point& center, double radius, double margin): OccupiedArea(Proxima(center, radius), margin) {
	}

	const Proxima& getProxima() const {
		return proxima;
	}

	double getMargin() const {
		return margin;
	}

	const std::vector<OccupiedRect>& getRects() const {
		return rects;
	}

	template<typename F>
	void setIsSameNet(F&& isSameNet) {
		sameNet = std::forward<F>(isSameNet);
	}

	void clearIsSameNet() {
		sameNet = nullptr;
	}

	bool add(const Avoid::Obstacle* obstacle) {
		if(nullptr == obstacle) return false;
		const Avoid::JunctionRef* junction = dynamic_cast<const Avoid::JunctionRef*>(obstacle);
		if(junction) {
			return addJunction(junction);
		} else {
			const Avoid::Box bb = obstacle->polygon().offsetBoundingBox(0.0);
			OccupiedRect r(bb.min.x - margin, bb.min.y - margin,
						   bb.max.x + margin, bb.max.y + margin,
						   "shape#" + std::to_string(obstacle->id()));
			if(getProxima().isNearEnough(r)) {
				rects.push_back(r);
				++shapeCount;
				return true;
			} else {
				++shapeSkipped;
			}
		}
		return false;
	}

	bool add(const Connector& connector) {
		if(!connector.isValid()) return false;
		if(isSameNet(connector.getId())) {
			++segmentSameNet;
			return false;
		}

		const std::vector<Avoid::Point>& pts = connector.getPoints();
		for(size_t i = 1; i < pts.size(); ++i) {
			const Avoid::Point& a = pts[i - 1];
			const Avoid::Point& b = pts[i];
			const bool isH = bend::isOrthogonalY(a, b);

			OccupiedRect r = isH ? OccupiedRect(std::min(a.x, b.x), std::min(a.y, b.y) - margin,
												std::max(a.x, b.x), std::max(a.y, b.y) + margin,
												true, "seg#" + std::to_string(connector.getId()))
								 : OccupiedRect(std::min(a.x, b.x) - margin, std::min(a.y, b.y),
												std::max(a.x, b.x) + margin, std::max(a.y, b.y),
												false, "seg#" + std::to_string(connector.getId()));

			if(getProxima().isNearEnough(r)) {
				rects.push_back(r);
				++segmentCount;
				return true;
			} else {
				++segmentSkipped;
			}
		}
		return false;
	}

	const OccupiedRect* getBlockedBy(const Avoid::Point& p) const {
		for(const OccupiedRect& r: rects)
			if(r.contains(p)) return &r;
		return nullptr;
	}

	inline bool isFree(const Avoid::Point& p) const {
		return nullptr == getBlockedBy(p);
	};

	/**
	 * Horizontal edge: wire arrives along Y=const from approach.x side.
	 * Need horizontal corridor from approach.x to candidate.x at candidate.y.
	 */
	const OccupiedRect* getHorizontalBlockedBy(const Avoid::Point& candidate, const Avoid::Point& approach) const {
		for(const OccupiedRect& r: rects) {
			if(r.blocksHorizontal(candidate.y, approach.x, candidate.x)) {
				return &r;
			}
		}
		return nullptr;
	}

	/**
	 * Vertical edge: wire arrives along X=const from approach.y side.
	 * Need vertical corridor from approach.y to candidate.y at candidate.x.
	 */
	const OccupiedRect* getVerticalBlockedBy(const Avoid::Point& candidate, const Avoid::Point& approach) const {
		for(const OccupiedRect& r: rects) {
			if(r.blocksVertical(candidate.x, approach.y, candidate.y)) {
				return &r;
			}
		}
		return nullptr;
	}

	/**
	 * Check vertical or horizontal corridor, depending on edge orthogonality.
	 */
	const OccupiedRect* getBlockedBy(const Avoid::Point& candidate, const Avoid::Edge& connectorEnd) const {
		// connectorEnd.a is at (or near) the junction, connectorEnd.b is one step away.
		const Avoid::Point& approach = connectorEnd.a;
		return isOrthogonalY(connectorEnd)
					   ? getHorizontalBlockedBy(candidate, approach)
					   : getVerticalBlockedBy(candidate, approach);
	}

	inline int getShapeCount() const {
		return shapeCount;
	}

	inline int getShapeSkipped() const {
		return shapeSkipped;
	}

	inline int getJunctionCount() const {
		return junctionCount;
	}

	inline int getJunctionSameNet() const {
		return junctionSameNet;
	}

	inline int getJunctionSkipped() const {
		return junctionSkipped;
	}

	inline int getSegmentCount() const {
		return segmentCount;
	}

	inline int getSegmentSameNet() const {
		return segmentSameNet;
	}

	inline int getSegmentSkipped() const {
		return segmentSkipped;
	}

protected:
	bool isSameNet(unsigned int id) const {
		if(!sameNet) return false;
		return sameNet(id);
	}

	bool addJunction(const Avoid::JunctionRef* junction) {
		if(isSameNet(junction->id())) {
			++junctionSameNet;
			return false;
		}
		const Avoid::Point pos = junction->recommendedPosition();
		OccupiedRect r(pos.x - margin, pos.y - margin,
					   pos.x + margin, pos.y + margin,
					   "junc#" + std::to_string(junction->id()));
		if(getProxima().isNearEnough(r)) {
			rects.push_back(r);
			++junctionCount;
			return true;
		} else {
			++junctionSkipped;
		}
		return false;
	}

private:
	Proxima proxima;
	double margin = 0;
	std::vector<OccupiedRect> rects;
	std::function<bool(unsigned int)> sameNet;
	int shapeCount = 0;
	int shapeSkipped = 0;
	int junctionCount = 0;
	int junctionSameNet = 0;
	int junctionSkipped = 0;
	int segmentCount = 0;
	int segmentSameNet = 0;
	int segmentSkipped = 0;
};
} // namespace bend