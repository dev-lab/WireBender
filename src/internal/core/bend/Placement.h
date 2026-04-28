/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Transform.h"
#include "libavoid/libavoid.h"
#include "utils.h"

namespace bend {

/**
 * Component placement (position, rotation).
 */
struct Placement {
	/**
	 * Calculate delta tranformation from this placement to target placement.
	 * @param targetPlacement target placement
	 * @return delta placement
	 */
	inline Placement delta(const Placement& targetPlacement) const {
		Placement result;
		positionDelta(position, targetPlacement.position, result.position);
		result.transform = transform.delta(targetPlacement.transform);
		return result;
	}

	/**
	 * Is position non-zero (any diminsion more than epsilon).
	 * @return true if position set
	 */
	inline bool isPosition() const {
		return position.x > bend::EPS || position.x < bend::NEPS || position.y > bend::EPS || position.y < bend::NEPS;
	}

	Avoid::Point position = {0, 0};
	Transform transform;

private:
	/**
	 * Calculates delta for position.
	 * @param current current position
	 * @param target target position
	 * @param result delta position
	 */
	inline static void positionDelta(const Avoid::Point& current, const Avoid::Point& target, Avoid::Point& result) {
		result.x = target.x - current.x;
		result.y = target.y - current.y;
	}
};
} // namespace bend