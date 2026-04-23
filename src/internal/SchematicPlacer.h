#pragma once
// internal/SchematicPlacer.h
// Thin adapter: converts WireBender types to prototype types,
// delegates to PlacementOptimizer (copied verbatim from prototype).
// PlacementOptimizer::place() updates comp.x/y in-place.

#include "../WireBenderTypes.h"
#include "SchematicAvoidRouter.h" // toComps / toRawNets helpers
#include "SchematicNetlist.h"
#include "core/NetList.h"
#include "core/PlacementOptimizer.h"

#include <map>
#include <string>

namespace WireBender {
namespace SchematicPlacer {

inline std::map<std::string, Placement> place(
		const SchematicNetlist& netlist,
		const std::vector<NetClassification>& classificationOverride,
		const std::map<std::string, Placement>& locks = {}) {
	// Locked components start at their target positions; others start at (0,0).
	std::map<std::string, Placement> startPlacements;
	for(const auto& c: netlist.components) {
		auto it = locks.find(c.id);
		startPlacements[c.id] = (it != locks.end())
										? it->second
										: Placement();
	}

	auto comps = toComps(netlist, startPlacements);
	const NetList nl(toRawNets(netlist), classificationOverride);

	PlacementOptimizer opt;
	std::map<std::string, bend::Placement> bendLocks;
	toBendPlacements(locks, bendLocks);
	opt.place(comps, nl, bendLocks);

	std::map<std::string, Placement> result;
	for(const auto& [id, comp]: comps) {
		result.emplace(id, fromBendPlacement(comp.placement));
	}
	return result;
}

}
} // namespace WireBender::SchematicPlacer
