#pragma once

#include "libavoid/libavoid.h"

namespace bend {

/**
 * Result of findBestJunctionPoint.
 */
struct JunctionPointResult {
	Avoid::Point point;
	/**
	 * true if no valid point was found within radius; point is the closest candidate encountered (may still overlap an obstacle).
	 * false point satisfies all clearance constraints.
	 */
	bool clamped = false;
};

} // namespace bend