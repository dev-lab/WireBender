/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "../WireBenderTypes.h"
#include "SchematicNetlist.h"

#include "core/NetList.h"
#include "core/RoutingResult.h"
#include "core/SchematicRouter.h"
#include "core/ShapeRegistry.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace WireBender {

/**
 * @brief Convert API Placement to implementation Placement.
 */
inline void toBendPlacement(const Placement& placement, bend::Placement& bendPlacement) {
	bendPlacement.position.x = placement.position.x;
	bendPlacement.position.y = placement.position.y;
	bendPlacement.transform = bend::Transform(placement.transform.rotation, placement.transform.flipX);
}

/**
 * @brief Convert API Placement to implementation Placement.
 */
inline bend::Placement toBendPlacement(const Placement& placement) {
	bend::Placement bendPlacement;
	toBendPlacement(placement, bendPlacement);
	return bendPlacement;
}

/**
 * @brief Convert map of component IDs -> API Placements to map of component IDs -> implementation Placements.
 */
inline void toBendPlacements(const std::map<std::string, Placement>& placements, std::map<std::string, bend::Placement>& bendPlacements) {
	bendPlacements.clear();
	for(const auto& [id, placement]: placements) {
		bendPlacements.emplace(id, toBendPlacement(placement));
	}
}

/**
 * @brief Convert implementation Placement to API Placement.
 */
inline void fromBendPlacement(const bend::Placement& bendPlacement, Placement& placement) {
	placement.position.x = bendPlacement.position.x;
	placement.position.y = bendPlacement.position.y;
	placement.transform.rotation = bendPlacement.transform.getRotation();
	placement.transform.flipX = bendPlacement.transform.isFlipX();
}

/**
 * @brief Convert implementation Placement to API Placement.
 */
inline Placement fromBendPlacement(const bend::Placement& bendPlacement) {
	Placement placement;
	fromBendPlacement(bendPlacement, placement);
	return placement;
}

/**
 * @brief Convert map of component IDs -> implementation Placements to map of component IDs -> API Placements.
 */
inline void fromBendPlacements(const std::map<std::string, bend::Placement>& bendPlacements, std::map<std::string, Placement>& placements) {
	placements.clear();
	for(const auto& [id, bendPlacement]: bendPlacements) {
		placements.emplace(id, fromBendPlacement(bendPlacement));
	}
}

/**
 * Convert a ComponentDescriptor (API) to a Component (implementation).
 */
inline Component toComp(const ComponentDescriptor& cd, const Placement& placement) {
	Component c;
	toBendPlacement(placement, c.placement);
	c.w = cd.width;
	c.h = cd.height;
	for(const auto& pd: cd.pins) {
		Pin p;
		p.number = pd.number;
		p.name = pd.name;
		// Convert local center-relative coordinates to top-left based
		p.x = pd.x + cd.width / 2.0;
		p.y = pd.y + cd.height / 2.0;
		p.dir = static_cast<Avoid::ConnDirFlags>(pd.directionFlags);
		c.pins.push_back(p);
	}
	std::sort(c.pins.begin(), c.pins.end(), [](const auto& a, const auto& b) {
		return a.number < b.number;
	});
	return c;
}

/**
 * Build implementation Comps map from API netlist + placements.
 */
inline Components toComps(
		const SchematicNetlist& netlist,
		const std::map<std::string, Placement>& placements) {
	Components comps;
	for(const auto& cd: netlist.components) {
		const auto& placement = placements.at(cd.id);
		comps[cd.id] = toComp(cd, placement);
	}
	return comps;
}

/**
 * Build implementation RawNets from API NetDescriptors.
 * @return map of net name to NetPin(componentId, pinNumber).
 */
inline RawNets toRawNets(const SchematicNetlist& netlist) {
	RawNets raw;
	for(const auto& nd: netlist.nets) {
		auto& netPins = raw[nd.name];
		for(const auto& pr: nd.pins)
			netPins.push_back({pr.componentId, (unsigned int)pr.pinNumber});
	}
	return raw;
}

/**
 * SchematicAvoidRouter — adapter class
 */
class SchematicAvoidRouter {
public:
	SchematicAvoidRouter(const SchematicNetlist& netlist,
						 const std::vector<NetClassification>& classificationOverride,
						 const std::map<std::string, Placement>& placements)
		: apiNetlist(netlist), apiClassificationOverride(classificationOverride), apiPlacements(placements) {}

	void buildAndRoute() {
		components = toComps(apiNetlist, apiPlacements);
		netlist = std::make_unique<NetList>(toRawNets(apiNetlist), apiClassificationOverride);
		router = std::make_unique<SchematicRouter>();
		router->build(components, *netlist);
	}

	void moveShape(const std::string& compId, const Placement& placement) {
		if(!router)
			throw std::logic_error("buildAndRoute() must be called before moveShape()");
		bend::Placement bendPlacement;
		toBendPlacement(placement, bendPlacement);
		router->moveComp(compId, bendPlacement);
		apiPlacements[compId] = placement;
	}

	RouterState getState() const {
		return router->getState();
	}

	bool printRoutingStats() const {
		if(!router) return true;
		return router->printStats(true);
	}

private:
	const SchematicNetlist& apiNetlist;
	std::vector<NetClassification> apiClassificationOverride;
	std::map<std::string, Placement> apiPlacements;

	Components components;
	std::unique_ptr<NetList> netlist;
	std::unique_ptr<SchematicRouter> router;
};

} // namespace WireBender
