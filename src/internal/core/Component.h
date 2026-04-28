/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Pin.h"
#include "bend/Placement.h"
#include <vector>
#include <map>

/**
 * Component.
 */
struct Component {
	/**
	 * @brief Get component width taking into account rotation.
	 */
	inline double getWidth() const {
		return placement.transform.isQuarterRotation() ? h : w;
	}

	/**
	 * @brief Get component height taking into account rotation.
	 */
	inline double getHeight() const {
		return placement.transform.isQuarterRotation() ? w : h;
	}

	/**
	 * @brief Check if point is inside component (taking into account rotation).
	 */
	inline bool isInside(double x, double y) const {
		const double ew = getWidth();
		const double eh = getHeight();
		const double cx = placement.position.x;
		const double cy = placement.position.y;
		return (x >= cx - ew / 2.0 && x <= cx + ew / 2.0 && y >= cy - eh / 2.0 && y <= cy + eh / 2.0);
	}

	bend::Placement placement; // component placement
	double w;				   // component width
	double h;				   // component height
	std::vector<Pin> pins;	   // component pins
};

// Map of component name per Component object
using Components = std::map<std::string, Component>;
