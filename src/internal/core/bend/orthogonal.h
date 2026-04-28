/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "libavoid/libavoid.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bend {

const double TOLERANCE = 0.05;

inline bool isOrthogonal(const double& cand, const double& adj) {
	return std::abs(cand - adj) < TOLERANCE;
}

inline double minGap(const double& v1, const double& v2, const double& gap) {
	return std::min(v1, v2) - gap;
}

inline double minT(const double& v1, const double& v2) {
	return minGap(v1, v2, TOLERANCE);
}

inline double maxGap(const double& v1, const double& v2, const double& gap) {
	return std::max(v1, v2) + gap;
}

inline double maxT(const double& v1, const double& v2) {
	return maxGap(v1, v2, TOLERANCE);
}

/**
 * @brief Is segment vertical?
 */
inline bool isOrthogonalX(const Avoid::Point& cand, const Avoid::Point& adj) {
	return isOrthogonal(cand.x, adj.x);
}

/**
 * @brief Is edge vertical?
 */
inline bool isOrthogonalX(const Avoid::Edge& cand) {
	return isOrthogonalX(cand.a, cand.b);
}

/**
 * @brief Is segment horizontal?
 */
inline bool isOrthogonalY(const Avoid::Point& cand, const Avoid::Point& adj) {
	return isOrthogonal(cand.y, adj.y);
}

/**
 * @brief Is edge horizontal?
 */
inline bool isOrthogonalY(const Avoid::Edge& cand) {
	return isOrthogonalY(cand.a, cand.b);
}

/**
 * @brief Is segment vertical or horizontal?
 */
inline bool isOrthogonal(const Avoid::Point& cand, const Avoid::Point& adj) {
	return isOrthogonalX(cand, adj) || isOrthogonalY(cand, adj);
}

/**
 * @brief Is edge vertical or horizontal?
 */
inline bool isOrthogonal(const Avoid::Edge& cand) {
	return isOrthogonal(cand.a, cand.b);
}

/**
 * @brief Are points virtually the same?
 */
inline bool isSame(const Avoid::Point& cand, const Avoid::Point& adj) {
	return isOrthogonalX(cand, adj) && isOrthogonalY(cand, adj);
}

/**
 * @brief Are points on the same vertical or horizontal line?
 */
inline bool isOrthogonalCollinear(const Avoid::Point& p1, const Avoid::Point& p2, const Avoid::Point& p) {
	if(isOrthogonalX(p1, p2)) {
		return isOrthogonalX(p1, p);
	}

	if(isOrthogonalY(p1, p2)) {
		return isOrthogonalY(p1, p);
	}

	return false;
}

/**
 * @brief Are points on the same vertical or horizontal line?
 */
inline bool isOrthogonalCollinear(const Avoid::Point& p1, const Avoid::Point& p2, const Avoid::Point& p3, const Avoid::Point& p4) {
	if(isOrthogonalX(p1, p2)) {
		return isOrthogonalX(p1, p3) && isOrthogonalX(p1, p4);
	}

	if(isOrthogonalY(p1, p2)) {
		return isOrthogonalY(p1, p3) && isOrthogonalY(p1, p4);
	}

	return false;
}

/**
 * @brief Is point on vertical or horizontal edge?
 */
inline bool isOrthogonalCollinear(const Avoid::Edge& e, const Avoid::Point& p) {
	return isOrthogonalCollinear(e.a, e.b, p);
}

/**
 * @brief Are edges on the same vertical or horizontal line?
 */
inline bool isOrthogonalCollinear(const Avoid::Edge& e1, const Avoid::Edge& e2) {
	return isOrthogonalCollinear(e1.a, e1.b, e2.a, e2.b);
}

/**
 * @brief is point inside of horizontal or vertical segment?
 * @param p1 segment point 1
 * @param p2 segment point 2
 * @param p point to be checked
 */
inline bool isOrthogonalBetween(const Avoid::Point& p1, const Avoid::Point& p2, const Avoid::Point& p) {
	if(isOrthogonalX(p1, p2)) {
		if(!isOrthogonalX(p1, p)) return false;
		return p.y >= minT(p1.y, p2.y) && p.y <= maxT(p1.y, p2.y);
	}

	if(isOrthogonalY(p1, p2)) {
		if(!isOrthogonalY(p1, p)) return false;
		return p.x >= minT(p1.x, p2.x) && p.x <= maxT(p1.x, p2.x);
	}

	return false;
}

/**
 * @brief is point on horizontal or vertical segment?
 * @param e segment (edge)
 * @param p point to be checked
 */
inline bool isOrthogonalBetween(const Avoid::Edge& e, const Avoid::Point& p) {
	return isOrthogonalBetween(e.a, e.b, p);
}

/**
 * Get the best orthogonal intersection of a set of orthogonal edges.
 * The intersection will try to reuse existing edges coordinates (that are probably aligned to grid, fit by libavoid).
 * @param edges two or more orthogonal edges (e.g. the near-end edges of each connector attached to a junction)
 * @return best representative point for junction placement
 */
inline Avoid::Point bestOrthogonalIntersection(const std::vector<Avoid::Edge>& edges) {
	size_t s = edges.size();
	if(0 == s) return Avoid::Point(0, 0);

	// Classify edges by orientation
	std::vector<size_t> horizontal; // horizontal edge indices
	std::vector<size_t> vertical;	// vertical edge indices

	for(size_t i = 0; i < s; ++i) {
		const Avoid::Edge& e = edges[i];
		if(isOrthogonalY(e)) {
			horizontal.push_back(i);
		} else if(isOrthogonalX(e)) {
			vertical.push_back(i);
		}
	}

	// Main path: at least one H and one V edge
	// max 2 values in each vector
	if(!horizontal.empty() && !vertical.empty()) {
		Avoid::Point result(edges[vertical[0]].a.x, edges[horizontal[0]].a.y);
		if(vertical.size() > 1) {
			double x1 = edges[vertical[1]].a.x;
			double xo = edges[horizontal[0]].a.x;
			if(abs(xo - x1) > abs(xo - result.x)) result.x = x1;
		}
		if(horizontal.size() > 1) {
			double y1 = edges[horizontal[1]].a.y;
			double yo = edges[vertical[0]].a.y;
			if(abs(yo - y1) > abs(yo - result.y)) result.y = y1;
		}
		return result;
	}

	// Fallback
	if(!horizontal.empty()) {
		return edges[horizontal[0]].a;
	} else if(!vertical.empty()) {
		return edges[vertical[0]].a;
	} else {
		// non-orthogonal fallback
		double sumX = 0.0, sumY = 0.0;
		for(const Avoid::Edge& e: edges) {
			sumX += e.a.x;
			sumY += e.a.y;
		}
		return Avoid::Point(sumX / static_cast<double>(s), sumY / static_cast<double>(s));
	}
}

} // namespace bend