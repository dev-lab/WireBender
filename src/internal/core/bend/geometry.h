/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace bend {

/**
 * @brief Computes the squared distance from point P to the line segment AB.
 *
 * Using squared distance avoids a square-root operation and is sufficient
 * for all comparison use-cases inside the post-processor.
 *
 * @tparam T point class (e.g. Point2D)
 * @param P  the query point
 * @param A  start of the segment
 * @param B  end of the segment
 * @return   squared Euclidean distance from P to the nearest point on AB
 */
template<class T>
double pointToSegmentDistSq(const T& P,
							const T& A,
							const T& B) {
	double l2 = (A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y);
	if(l2 == 0.0)
		return (P.x - A.x) * (P.x - A.x) + (P.y - A.y) * (P.y - A.y);

	double t = ((P.x - A.x) * (B.x - A.x) + (P.y - A.y) * (B.y - A.y)) / l2;
	t = std::max(0.0, std::min(1.0, t));

	double projX = A.x + t * (B.x - A.x);
	double projY = A.y + t * (B.y - A.y);
	return (P.x - projX) * (P.x - projX) + (P.y - projY) * (P.y - projY);
}

/**
 * @brief Tests whether the ordered triple (A, B, C) is in counter-clockwise
 *        orientation.
 *
 * This is the standard cross-product sign test used as a building block for
 * segment intersection detection.
 *
 * @tparam T point class (e.g. Point2D)
 * @param A first point
 * @param B second point
 * @param C third point
 * @return  true if the turn A→B→C is counter-clockwise
 */
template<class T>
bool ccw(const T& A, const T& B, const T& C) {
	return (C.y - A.y) * (B.x - A.x) > (B.y - A.y) * (C.x - A.x);
}

/**
 * @brief Tests whether segment AB properly intersects segment CD.
 *
 * "Properly" means the interiors cross; shared endpoints are excluded to
 * avoid false positives at wire junctions.
 *
 * @tparam T point class (e.g. Point2D)
 * @param A start of the first segment
 * @param B end of the first segment
 * @param C start of the second segment
 * @param D end of the second segment
 * @return  true if the two segments properly intersect
 */
template<class T>
bool segmentsIntersect(const T& A, const T& B,
					   const T& C, const T& D) {
	bool intersect = (ccw(A, C, D) != ccw(B, C, D)) && (ccw(A, B, C) != ccw(A, B, D));
	if(!intersect) return false;

	auto isSame = [](const T& p1, const T& p2) {
		return std::abs(p1.x - p2.x) < 1e-3 && std::abs(p1.y - p2.y) < 1e-3;
	};
	if(isSame(A, C) || isSame(A, D) || isSame(B, C) || isSame(B, D))
		return false;

	return true;
}

/**
 * @brief Computes the intersection point of the lines defined by AB and CD.
 *
 * Uses Cramer's rule on the implicit line equations.  The caller must ensure
 * that the lines are not parallel (determinant ≠ 0); if they are, point A is
 * returned as a safe fallback.
 *
 * @tparam T point class (e.g. Point2D)
 * @param A start of the first line segment
 * @param B end of the first line segment
 * @param C start of the second line segment
 * @param D end of the second line segment
 * @return  intersection point of the two lines, or A if lines are parallel
 */
template<class T>
T computeIntersection(const T& A, const T& B,
					  const T& C, const T& D) {
	double a1 = B.y - A.y, b1 = A.x - B.x, c1 = a1 * A.x + b1 * A.y;
	double a2 = D.y - C.y, b2 = C.x - D.x, c2 = a2 * C.x + b2 * C.y;
	double det = a1 * b2 - a2 * b1;
	if(std::abs(det) < 1e-9) return A;
	return {(b2 * c1 - b1 * c2) / det,
			(a1 * c2 - a2 * c1) / det};
}

/**
 * @brief Computes the squared distance between two line segments AB and CD.
 *
 * If the segments intersect the distance is zero.  Otherwise it is the
 * minimum of the four point-to-segment distances.
 *
 * @tparam T point class (e.g. Point2D)
 * @param A start of the first segment
 * @param B end of the first segment
 * @param C start of the second segment
 * @param D end of the second segment
 * @return  squared minimum distance between the two segments
 */
template<class T>
double segmentToSegmentDistSq(const T& A, const T& B,
							  const T& C, const T& D) {
	if(segmentsIntersect(A, B, C, D)) return 0.0;
	return std::min({pointToSegmentDistSq(A, C, D),
					 pointToSegmentDistSq(B, C, D),
					 pointToSegmentDistSq(C, A, B),
					 pointToSegmentDistSq(D, A, B)});
}

/**
 * Rectangle.
 */
struct Rect {
	double x1, y1, x2, y2;

	/**
	 * @brief does it overlap with another rectangle?
	 */
	bool overlaps(const Rect& other, double inflate = 0.0) const {
		return x1 - inflate < other.x2 && x2 + inflate > other.x1
			   && y1 - inflate < other.y2 && y2 + inflate > other.y1;
	}
};


} // namespace bend
