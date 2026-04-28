/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "OccupiedRect.h"
#include "libavoid/libavoid.h"
#include "utils.h"

namespace bend {

class Proxima {
public:
	inline Proxima(const Avoid::Point& center, double radius): center(center), radius(radius), radius2(radius * radius) {
	}

	inline const Avoid::Point& getCenter() const {
		return center;
	}

	inline double getRadius() const {
		return radius;
	}

	inline double getRadius2() const {
		return radius2;
	}

	inline bool isNearEnough(const double& x, const double& y) const {
		return distance2(x - center.x, y - center.y) <= radius2;
	};

	inline bool isNearEnough(const Avoid::Point& p) const {
		return isNearEnough(p.x, p.y);
	};

	inline bool isNearEnough(const OccupiedRect& r) const {
		double cx = std::max(r.minX, std::min(center.x, r.maxX));
		double cy = std::max(r.minY, std::min(center.y, r.maxY));
		return isNearEnough(cx, cy);
	}

private:
	Avoid::Point center;
	double radius;

	double radius2;
};
} // namespace bend