/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

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