/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "WireBenderTypes.h"
#include "internal/SchematicNetlist.h"
#include "internal/core/bend/utils.h"
#include "internal/core/bend/geometry.h"
#include "internal/core/bend/Transform.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace WireBender {

/**
 * Create label hints (placements).
 */
class Labelbot {
public:
	// Estimated text bounding-box half-extents (world units).
	// These are intentionally generous — better to leave a little more room
	// than to place a label that partially overlaps something.
	static constexpr double REF_HW = 22.0; // half-width  (≈ 4–6 char ref at 10 px)
	static constexpr double REF_HH = 7.0;  // half-height
	static constexpr double VAL_HW = 20.0; // half-width  (≈ value string at 9 px)
	static constexpr double VAL_HH = 6.0;  // half-height
	static constexpr double PADDING = 6.0; // clearance between box edge and label

	inline static bool isQuarterRotation(const Transform& transform) {
		int rot = bend::Transform::normalizeRotation(transform.rotation);
		return rot == 90 || rot == 270;
	}

	inline static double getWidth(const ComponentDescriptor& comp, const Transform& transform) {
		return isQuarterRotation(transform) ? comp.height : comp.width;
	}

	inline static double getHeight(const ComponentDescriptor& comp, const Transform& transform) {
		return isQuarterRotation(transform) ? comp.width : comp.height;
	}

	/**
	 * Create Component Labels.
	 * @param components components to be labeled
	 * @param placements component placements
	 * @param wires net wires
	 */
	explicit Labelbot(const std::vector<ComponentDescriptor>& components,
					  const std::map<std::string, Placement>& placements,
					  const SchematicNetlist& netlist,
					  const std::vector<Wire>& wires) noexcept
		: components(components), placements(placements), netlist(netlist), wires(wires) {
	}

	/**
	 * Build ComponentLabelHints for all components and append to labels.
	 * @param labels result labels
	 */
	void annotate(std::vector<ComponentLabelHint>& labels) const {
		for(const auto& comp: components) {
			auto it = placements.find(comp.id);
			if(it == placements.end()) continue;
			labels.push_back(componentLabelHint(comp, it->second));
		}
	}

	/**
	 * Build ComponentLabelHint for one component and append to labels.
	 * @param
	 */
	bool annotate(std::vector<ComponentLabelHint>& labels, std::string componentId, const Placement& placement) const {
		auto compIt = std::find_if(components.begin(),
								   components.end(),
								   [&](const ComponentDescriptor& c) {
									   return c.id == componentId;
								   });
		if(compIt != components.end()) {
			labels.push_back(componentLabelHint(*compIt, placement));
			return true;
		}
		return false;
	}

	/**
	 * Build label hints for all nets.
	 * @param netLabels result net labels
	 */
	void annotateNets(std::vector<NetLabelHint>& netLabels) const {
		std::set<std::string> seen;
		for(const auto& net: netlist.nets) {
			if(seen.count(net.name)) continue;
			seen.insert(net.name);
			netLabels.push_back(netLabelHint(net.name));
		}
	}

	/**
	 * Build label hints for selected nets.
	 * @param netLabels result net labels
	 * @param netNames net names to build labels
	 */
	template <class T>
	void annotateNets(std::vector<NetLabelHint>& netLabels, const T& netNames) {
		for(const auto& net: netNames) {
			netLabels.push_back(netLabelHint(net));
		}
	}

protected:
	/**
	 * @brief does `r` collide with any component box or any wire segment?
	 */
	bool collidesWithScene(const bend::Rect& r, const std::string& skipCompId) const {
		constexpr double BBOX_INFLATE = 4.0;
		// Check components
		for(const auto& comp: components) {
			if(comp.id == skipCompId) continue;
			auto it = placements.find(comp.id);
			if(it == placements.end()) continue;
			const auto& placement = it->second;
			const Point2D& pos = placement.position;
			const double w = getWidth(comp, placement.transform);
			const double h = getHeight(comp, placement.transform);
			bend::Rect compRect{pos.x - w / 2 - BBOX_INFLATE,
						  pos.y - h / 2 - BBOX_INFLATE,
						  pos.x + w / 2 + BBOX_INFLATE,
						  pos.y + h / 2 + BBOX_INFLATE};
			if(r.overlaps(compRect)) return true;
		}
		// Check wire segments
		constexpr double WIRE_INFLATE = 2.0;
		for(const auto& w: wires) {
			if(!w.points.empty())
				for(size_t i = 0; i + 1 < w.points.size(); ++i) {
					const auto& p1 = w.points[i];
					const auto& p2 = w.points[i + 1];
					bend::Rect seg{std::min(p1.x, p2.x) - WIRE_INFLATE,
							 std::min(p1.y, p2.y) - WIRE_INFLATE,
							 std::max(p1.x, p2.x) + WIRE_INFLATE,
							 std::max(p1.y, p2.y) + WIRE_INFLATE};
					if(r.overlaps(seg)) return true;
				}
		}
		return false;
	}

	/**
	 * Build ComponentLabelHint for one component.
	 *
	 * For each component we generate a small list of candidate positions
	 * (above, below, right, left of the bounding box, with PADDING clearance)
	 * then pick the first one that does not overlap any wire segment or other
	 * component box.  The two labels (ref and value) must also not overlap each
	 * other, so the value search excludes the rect already claimed by ref.
	 *
	 * Coordinate convention: component positions are box centres; pin coords
	 * are component-local (origin = centre).
	 * All output positions are world-space centre anchors for the text.
	 * 
	 * Approximate text dimensions (world units) are passed in so the caller can
	 * tune them; a sensible default is ~refW=40, refH=12, valW=36, valH=11.
	 */
	ComponentLabelHint componentLabelHint(const ComponentDescriptor& comp, const Placement& placement) const {
		const Point2D& pos = placement.position;
		const double hw = getWidth(comp, placement.transform) / 2.0;	 // component half-width
		const double hh = getHeight(comp, placement.transform) / 2.0; // component half-height

		// Candidate centre positions (world coords), tried in priority order:
		//	0 above-centre, 1 below-centre, 2 right-centre, 3 left-centre,
		//	4 above-right,	5 above-left,	6 below-right,	7 below-left
		const auto refCandidates = std::vector<Point2D>{
				{pos.x, pos.y - hh - REF_HH - PADDING},		 // above
				{pos.x, pos.y + hh + REF_HH + PADDING},		 // below
				{pos.x + hw + REF_HW + PADDING, pos.y},		 // right
				{pos.x - hw - REF_HW - PADDING, pos.y},		 // left
				{pos.x + hw + REF_HW + PADDING, pos.y - hh}, // above-right
				{pos.x - hw - REF_HW - PADDING, pos.y - hh}, // above-left
		};

		// Find first non-colliding ref candidate
		Point2D refPos = refCandidates[0];
		bool refVertOnly = false; // TODO: set to true if label shall be rotated 90 degrees
		int refChosenIdx = 0;
		for(int i = 0; i < (int)refCandidates.size(); ++i) {
			const auto& c = refCandidates[i];
			bend::Rect r{c.x - REF_HW, c.y - REF_HH, c.x + REF_HW, c.y + REF_HH};
			if(!collidesWithScene(r, comp.id)) {
				refPos = c;
				refChosenIdx = i;
				break;
			}
		}
		const bend::Rect refRect{refPos.x - REF_HW, refPos.y - REF_HH,
						   refPos.x + REF_HW, refPos.y + REF_HH};

		// Value candidates: prefer the opposite side from ref to separate them
		const auto valCandidates = std::vector<Point2D>{
				{pos.x, pos.y + hh + VAL_HH + PADDING},		 // below
				{pos.x, pos.y - hh - VAL_HH - PADDING},		 // above
				{pos.x + hw + VAL_HW + PADDING, pos.y},		 // right
				{pos.x - hw - VAL_HW - PADDING, pos.y},		 // left
				{pos.x + hw + VAL_HW + PADDING, pos.y + hh}, // below-right
				{pos.x - hw - VAL_HW - PADDING, pos.y + hh}, // below-left
		};

		Point2D valPos = valCandidates[0];
		bool valVertOnly = false; // TODO: set to true if label shall be rotated 90 degrees
		for(int i = 0; i < (int)valCandidates.size(); ++i) {
			const auto& c = valCandidates[i];
			bend::Rect r{c.x - VAL_HW, c.y - VAL_HH, c.x + VAL_HW, c.y + VAL_HH};
			// Must not collide with scene *or* with the already-placed ref rect
			if(!collidesWithScene(r, comp.id) && !r.overlaps(refRect)) {
				valPos = c;
				break;
			}
		}

		return ComponentLabelHint{comp.id, refPos, refVertOnly, valPos, valVertOnly};
	}

	/**
	 * Compute label hint for a net: midpoint of its longest wire segment.
	 * @param net net name
	 * @param wires all wires (not only those belonging to the net we calculate the label for)
	 * @return net label hint
	 */
	NetLabelHint netLabelHint(const std::string& net) const {
		NetLabelHint hint;
		hint.net = net;
		double bestLen = -1.0;
		for(const auto& w: wires) {
			if(w.net != net || w.points.size() < 2) continue;
			for(size_t i = 0; i + 1 < w.points.size(); ++i) {
				const auto& a = w.points[i];
				const auto& b = w.points[i + 1];
				const double dx = b.x - a.x, dy = b.y - a.y;
				const double len = dx * dx + dy * dy;
				if(len > bestLen) {
					bestLen = len;
					hint.position = {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
					hint.isVertical = bend::is0(dx);
				}
			}
		}
		return hint;
	}

private:
	const std::vector<ComponentDescriptor>& components;
	const std::map<std::string, Placement>& placements;
	const SchematicNetlist& netlist;
	const std::vector<Wire>& wires;
};

} // namespace WireBender