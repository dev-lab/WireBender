/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#include "WireBender.h"

#include "internal/Labelbot.h"
#include "internal/NetClassifier.h"
#include "internal/SchematicAvoidRouter.h"
#include "internal/SchematicNetlist.h"
#include "internal/SchematicPlacer.h"
#include "internal/core/RoutingResult.h"

#include <algorithm>
#include <ctime>
#include <set>
#include <stdexcept>

namespace WireBender {

/**
 * Convert prototype RoutingResult to WireBender SchematicRouteResult.
 * @param rr routing result to be converted
 * @return schematic route result (to be returned by API)
 */
static SchematicRouteResult toRouteResult(const RoutingResult& rr) {
	SchematicRouteResult result;
	for(const auto& [n, wires]: rr.getWires()) {
		for(const auto& w: wires) {
			Wire wire;
			wire.net = n;
			for(const auto& pt: w) {
				wire.points.push_back({pt.x, pt.y});
			}
			result.wires.push_back(std::move(wire));
		}
	}
	for(const auto& [n, dots]: rr.getDots()) {
		for(const auto& d: dots) {
			result.junctions.push_back({n, {d.x, d.y}});
		}
	}
	return result;
}

/**
 * WireBender API implementation  — owns all mutable routing state.
 */
class WireBenderImpl {
public:
	/**
	 * Ensure classification is up to date.
	 */
	void ensureClassification() {
		if(!classificationApplied) {
			classification = NetClassifier::classify(netlist);
			classificationDiff.clear();
			classificationApplied = true;
		}
	}

	void applyClassification(const std::vector<NetClassification>& userClassification) {
		classification = userClassification;
		classificationDiff = NetClassifier::classifyDiff(netlist, classification);
		classificationApplied = true;
	}

	/**
	 * Find which nets are connected to a given component.
	 * @param compId component ID
	 * @return nets connected to this component
	 */
	std::set<std::string> netsForComponent(const std::string& compId) const {
		std::set<std::string> result;
		for(const auto& net: netlist.nets) {
			for(const auto& pin: net.pins) {
				if(pin.componentId == compId) {
					result.insert(net.name);
					break;
				}
			}
		}
		return result;
	}

	/**
	 * Apply pin mapping to all nets after a component replacement.
	 * @param rep component replacement
	 * @return names of nets that were modified (for selective re-routing)
	 */
	std::vector<std::string> applyPinMapping(const ComponentReplacement& rep) {
		std::vector<std::string> affected;
		for(auto& net: netlist.nets) {
			bool changed = false;
			for(auto& pin: net.pins) {
				if(pin.componentId != rep.componentId) continue;
				auto it = rep.pinMapping.oldToNew.find(pin.pinNumber);
				if(it == rep.pinMapping.oldToNew.end()) {
					// Pin removed — mark for erasure (set to sentinel -1)
					pin.pinNumber = -1;
					changed = true;
				} else if(it->second != pin.pinNumber) {
					pin.pinNumber = it->second;
					changed = true;
				}
			}
			if(changed) {
				// Remove pins that were mapped away (sentinel -1)
				net.pins.erase(
						std::remove_if(net.pins.begin(), net.pins.end(),
									   [](const PinRef& p) {
										   return p.pinNumber == -1;
									   }),
						net.pins.end());
				affected.push_back(net.name);
			}
		}
		return affected;
	}

	/**
	 * Print routing stats to output.
	 * @return true if routing result was ok
	 */
	bool printRoutingStats() const {
		return router->printRoutingStats();
	}

	SchematicNetlist netlist;						   // Netlist description (plain data, no adaptagrams)
	std::vector<NetClassification> classification;	   // Net classification
	std::vector<NetClassification> classificationDiff; // User classification overrides
	bool classificationApplied = false;				   // Is classification applied
	ComponentPlacements placements;					   // Component world placements (set by computePlacement()/setComponentPlacement())
	ComponentPlacements lockedPlacements;			   // Locked placements — components held immovable during computePlacement()
	std::unique_ptr<SchematicAvoidRouter> router;	   // Live router (rebuilt on routeAll() / replaceComponent())
	SchematicRouteResult lastFullResult;			   // Last full routing result — used as base for incremental updates
	bool routed = false;							   // Is routed?
};

WireBender::WireBender(): impl(std::make_unique<WireBenderImpl>()) {}
WireBender::~WireBender() = default;

void WireBender::addComponent(const ComponentDescriptor& comp) {
	// Replace if exists
	auto& comps = impl->netlist.components;
	auto it = std::find_if(comps.begin(), comps.end(),
						   [&](const ComponentDescriptor& c) {
							   return c.id == comp.id;
						   });
	if(it != comps.end()) {
		*it = comp;
	} else {
		comps.push_back(comp);
	}

	// Invalidate downstream state
	impl->classificationApplied = false;
	impl->routed = false;
}

void WireBender::addNet(const NetDescriptor& net) {
	auto& nets = impl->netlist.nets;
	auto it = std::find_if(nets.begin(), nets.end(),
						   [&](const NetDescriptor& n) {
							   return n.name == net.name;
						   });
	if(it != nets.end()) {
		*it = net;
	} else {
		nets.push_back(net);
	}

	impl->classificationApplied = false;
	impl->routed = false;
}

void WireBender::clear() {
	impl = std::make_unique<WireBenderImpl>();
}

std::vector<NetClassification> WireBender::classify() const {
	impl->classificationApplied = false;
	impl->ensureClassification();
	return impl->classification;
}

void WireBender::applyClassification(const std::vector<NetClassification>& cls) {
	impl->applyClassification(cls);
	impl->routed = false;
}

void WireBender::setLockedPlacements(const ComponentPlacements& locks) {
	impl->lockedPlacements = locks;
}

ComponentPlacements WireBender::computePlacements() {
	impl->ensureClassification();
	impl->placements.placements = SchematicPlacer::place(impl->netlist,
														 impl->classificationDiff,
														 impl->lockedPlacements.placements);
	return impl->placements;
}

void WireBender::setComponentPlacement(const std::string& componentId, const Placement& placement) {
	impl->placements.placements[componentId] = placement;
}

void WireBender::setPlacements(const ComponentPlacements& placements) {
	impl->placements = placements;
}

SchematicRouteResult WireBender::routeAll() {
	impl->ensureClassification();

	if(impl->placements.placements.empty())
		throw std::logic_error("WireBender::routeAll(): no placements set. "
							   "Call computePlacements() or setPlacements() first.");

	impl->router = std::make_unique<SchematicAvoidRouter>(
			impl->netlist, impl->classificationDiff, impl->placements.placements);
	impl->router->buildAndRoute();

	const RoutingResult rr(impl->router->getState());
	SchematicRouteResult result = toRouteResult(rr);
	Labelbot labelbot(impl->netlist.components, impl->placements.placements, impl->netlist, result.wires);
	labelbot.annotateNets(result.netLabels);
	labelbot.annotate(result.componentLabels);

	impl->lastFullResult = result;
	impl->routed = true;
	return result;
}

IncrementalRouteResult WireBender::moveComponent(const std::string& componentId, const Placement& placement) {
	if(!impl->routed)
		throw std::logic_error("WireBender::moveComponent(): call routeAll() first.");

	impl->placements.placements[componentId] = placement;
	impl->router->moveShape(componentId, placement);

	const std::set<std::string> affectedNets = impl->netsForComponent(componentId);
	const RoutingResult rr(impl->router->getState(), affectedNets);

	IncrementalRouteResult result;
	result.affectedNets = std::vector<std::string>(affectedNets.begin(), affectedNets.end());
	result.routes = toRouteResult(rr);
	Labelbot labelbot(impl->netlist.components, impl->placements.placements, impl->netlist, result.routes.wires);
	labelbot.annotateNets(result.routes.netLabels, affectedNets);
	labelbot.annotate(result.routes.componentLabels, componentId, placement);
	return result;
}

SchematicRouteResult WireBender::replaceComponent(const ComponentReplacement& rep) {
	// 1. Replace component descriptor in netlist
	auto& comps = impl->netlist.components;
	auto it = std::find_if(comps.begin(), comps.end(),
						   [&](const ComponentDescriptor& c) {
							   return c.id == rep.componentId;
						   });
	if(it == comps.end())
		throw std::invalid_argument("WireBender::replaceComponent(): component '"
									+ rep.componentId + "' not found.");
	*it = rep.newDescriptor;
	it->id = rep.componentId; // ensure id is preserved

	// 2. Apply pin mapping to all nets
	impl->applyPinMapping(rep);

	// 3. Remove nets that now have fewer than 2 pins (effectively disconnected)
	auto& nets = impl->netlist.nets;
	nets.erase(std::remove_if(nets.begin(), nets.end(),
							  [](const NetDescriptor& n) {
								  return n.pins.size() < 2;
							  }),
			   nets.end());

	// 5. Re-route everything
	impl->routed = false;
	return routeAll();
}

bool WireBender::printRoutingStats() const {
	return impl->printRoutingStats();
}

} // namespace WireBender
