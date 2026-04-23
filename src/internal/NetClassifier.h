#pragma once
// internal/NetClassifier.h
// Thin adapter: converts WireBender SchematicNetlist to prototype NetList,
// then extracts classification results from it.

#include "SchematicAvoidRouter.h" // toRawNets helper
#include "SchematicNetlist.h"
#include "WireBenderTypes.h"
#include "core/NetList.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace WireBender {
namespace NetClassifier {

inline std::vector<NetClassification> classify(const SchematicNetlist& netlist) {
	const NetList nl(toRawNets(netlist));

	std::vector<NetClassification> result;
	for(const auto& name: nl.netNames()) {
		NetClassification c;
		c.name = name;
		c.isBus = nl.isBus(name);
		c.isGround = nl.isGnd(name);
		c.isPositive = nl.isPositiveRail(name);
		c.busLevel = nl.isBus(name) ? nl.busLevel(name) : -1;
		result.push_back(c);
	}
	return result;
}

inline std::vector<NetClassification> classifyDiff(const SchematicNetlist& netlist, const std::vector<NetClassification>& userClassification) {
	const NetList nl(toRawNets(netlist));
	std::vector<NetClassification> result;
	for(size_t i = 0; i < userClassification.size(); ++i) {
		const NetClassification& c = userClassification[i];
		if(nl.isBus(c.name) != c.isBus) {
			result.push_back(c);
			continue;
		}
		if(c.isBus) {
			if(nl.isGnd(c.name) != c.isGround
			   || nl.isPositiveRail(c.name) != c.isPositive
			   || nl.busLevel(c.name) != c.busLevel) {
				result.push_back(c);
				continue;
			}
		}
	}
	return result;
}
}
} // namespace WireBender::NetClassifier
