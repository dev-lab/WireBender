#pragma once

#include "libavoid/libavoid.h"
#include <cmath>
#include <string>

namespace bend {

/**
 * Axis-aligned bounding box used to represent occupied regions.
 *
 * Segment obstacles carry orientation so that corridor checks can ignore
 * perpendicular crossings (a vertical wire crossing a horizontal corridor
 * is always fine in orthogonal schematics).
 *
 * Shapes and junction point obstacles block all directions (omniDirectional).
 */
struct OccupiedRect {
	/**
	 * Constructor for omni-directional obstacles (shapes, junction points).
	 */
	OccupiedRect(double x0, double y0, double x1, double y1, std::string tag)
		: minX(std::min(x0, x1)), minY(std::min(y0, y1)), maxX(std::max(x0, x1)), maxY(std::max(y0, y1)), isHorizontal(false), omniDirectional(true), tag(std::move(tag)) {}

	/**
	 * Constructor for wire segment obstacles — orientation must be supplied.
	 */
	OccupiedRect(double x0, double y0, double x1, double y1,
				 bool isHorizontal_, std::string tag)
		: minX(std::min(x0, x1)), minY(std::min(y0, y1)), maxX(std::max(x0, x1)), maxY(std::max(y0, y1)), isHorizontal(isHorizontal_), omniDirectional(false), tag(std::move(tag)) {}

	bool contains(const Avoid::Point& p) const {
		return p.x >= minX && p.x <= maxX
			   && p.y >= minY && p.y <= maxY;
	}

	/**
	 * Does this obstacle block a vertical corridor at x, between y0 and y1?
	 * Horizontal wire segments are ignored (perpendicular crossing — always ok).
	 */
	bool blocksVertical(double x, double y0, double y1) const {
		if(!omniDirectional && isHorizontal) return false; // perpendicular crossing
		double loY = std::min(y0, y1), hiY = std::max(y0, y1);
		return minX <= x && x <= maxX
			   && minY <= hiY && maxY >= loY;
	}

	/**
	 * Does this obstacle block a horizontal corridor at y, between x0 and x1?
	 * Vertical wire segments are ignored (perpendicular crossing — always ok).
	 */
	bool blocksHorizontal(double y, double x0, double x1) const {
		if(!omniDirectional && !isHorizontal) return false; // perpendicular crossing
		double loX = std::min(x0, x1), hiX = std::max(x0, x1);
		return minY <= y && y <= maxY
			   && minX <= hiX && maxX >= loX;
	}

	std::string toString() const {
		std::string orient = omniDirectional ? "omni" : (isHorizontal ? "H" : "V");
		return "[" + std::to_string(bend::iround(minX))
			   + "," + std::to_string(bend::iround(minY))
			   + "-" + std::to_string(bend::iround(maxX))
			   + "," + std::to_string(bend::iround(maxY))
			   + "](" + tag + "," + orient + ")";
	}

	double minX;
	double minY;
	double maxX;
	double maxY;
	bool isHorizontal = false;	 // orientation of the underlying wire segment (when omniDirectional == false)
	bool omniDirectional = true; // true for shapes and foreign junctions
	std::string tag;			 // Human-readable tag for logging
};

} // namespace bend