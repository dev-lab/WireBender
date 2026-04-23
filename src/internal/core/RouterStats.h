#pragma once

/**
 * Router stats.
 * Booleans returned by Avoid::Router.exists*() methods not enough for real world.
 */
struct RouterStats {
	int orthogonalSegmentOverlapCount = 0;			 // orthogonal segment overlap count
	int orthogonalSegmentOverlapAtEndCount = 0;		 // orthogonal segment overlap count
	int orthogonalFixedSegmentOverlapCount = 0;		 // orthogonal fixed segment overlap count
	int orthogonalFixedSegmentOverlapAtEndCount = 0; // orthogonal fixed segment overlap count
	int orthogonalTouchingSegmentCount = 0;			 // orthogonal segments touching other segments (at path bend) count
	int invalidOrthogonalSegmentCount = 0;			 // invalid orthogonal segment count
	int crossingCount = 0;							 // crossing count

	bool isOk() const {
		return invalidOrthogonalSegmentCount == 0 && orthogonalSegmentOverlapCount == 0;
	}
};