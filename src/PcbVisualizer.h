/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "WireBenderTypes.h"
#include <memory>

namespace WireBender {

class PcbVisualizerImpl;

/**
 * Routes pad clusters on a PCB photo as minimum Steiner trees.
 * No component obstacles, no orthogonal constraint.
 * Multiple nets are routed simultaneously with crossing minimisation.
 * Usage:
 *	 WireBender::PcbVisualizer pcb;
 *	 pcb.addNet({"VCC", {{10,20},{50,80},{90,30}}});
 *	 pcb.addNet({"GND", {{15,25},{55,85}}});
 *	 auto result = pcb.route();
 */
class PcbVisualizer {
public:
	PcbVisualizer();
	~PcbVisualizer();

	PcbVisualizer(const PcbVisualizer&) = delete;
	PcbVisualizer& operator=(const PcbVisualizer&) = delete;

	/**
	 * Add a net with its pad positions. Multiple calls allowed.
	 * @param net PCB net to be added
	 */
	void addNet(const PcbNet& net);

	/**
	 * Remove all nets and reset state.
	 */
	void clear();

	/**
	 * Route all nets. The routing minimises crossings between different nets where possible.
	 * @return wires and junction dots
	 */
	PcbRouteResult route();

private:
	std::unique_ptr<PcbVisualizerImpl> impl;
};

} // namespace WireBender
