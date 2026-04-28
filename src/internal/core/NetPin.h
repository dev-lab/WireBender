/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include <string>

/**
 * Raw component pin, used by NetList.
 */
struct NetPin {
	std::string comp; // component name
	unsigned int pin; // pin number
};
