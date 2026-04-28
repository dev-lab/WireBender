/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Component.h"
#include "NetList.h"

#include "libcola/cola.h"
#include "libvpsc/rectangle.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * PlacementOptions - all tunable knobs for placement.
 */
struct PlacementOptions {
	// ── Padding & gaps ───────────────────────────────────────────────────────

	// Clearance added on every side of a component box.
	double componentPad = 16.0;

	// Extra space added to the padded bounding box of components that carry
	// at least one signal wire, to leave room for orthogonal routing.
	double routingGap = 40.0;

	// Extra space for components with no signal connections (bus-only or
	// completely isolated).  Usually 0 so they pack tightly against their
	// neighbours.
	double isolatedGap = 0.0;

	// ── libcola physics ──────────────────────────────────────────────────────

	// Ideal spring length passed to ConstrainedFDLayout.  Larger values spread
	// components further apart; smaller values pull them together.
	double idealEdgeLength = 150.0;

	// ── Initial positions ────────────────────────────────────────────────────

	// Components are staggered diagonally at startup to give the physics
	// solver a non-degenerate initial state.  Each component is offset by
	// (index * initialStagger) in both X and Y.
	double initialStagger = 5.0;

	// ── Post-placement normalisation ─────────────────────────────────────────

	// After solving, coordinates are shifted so the top-left component sits
	// at (canvasOrigin, canvasOrigin).
	double canvasOrigin = 20.0;

	// ── Topology options ─────────────────────────────────────────────────────

	// When true (default), bus nets are excluded from the spring-graph so they
	// don't cluster all bus-connected components into a single pile.
	bool excludeBusesFromEdges = true;

	// When true (default), a "gravity tether" edge is added from each
	// completely isolated component to the most-connected component, so
	// isolated parts don't fly off to infinity.
	bool addGravityTethers = true;
};

/**
 * PlacementOptimizer
 * Component placement optimizer using libcola (force-directed physics) and
 * libvpsc (overlap removal).
 *
 * Each Component carries a bend::Placement (position + transform).  The
 * optimizer only updates the position part; the transform (rotation/flip) is
 * always preserved as-is.  When a component is rotated 90° or 270° its
 * effective bounding box presented to libcola has w and h swapped, so that
 * the physics solver and overlap-removal work in world-space dimensions.
 *
 * Usage:
 *   PlacementOptions opts;
 *   opts.idealEdgeLength = 200;   // spread components further apart
 *   opts.routingGap      = 50;    // more room around components for wires
 *
 *   PlacementOptimizer optimizer(opts);
 *   optimizer.place(comps, nl);   // comps placements are updated in-place
 */
class PlacementOptimizer {
public:
	/**
	 * Create PlacementOptimizer.
	 */
	explicit PlacementOptimizer(const PlacementOptions& opts = PlacementOptions{})
		: opts(opts) {}

	/**
	 * Run placement.
	 * @param comps  Component placements are updated in-place (position only;
	 *               transform is preserved).  All other fields (w, h, pins) are
	 *               left unchanged.
	 * @param nl     Net lists.
	 * @param locked Maps componentId to a locked bend::Placement (position +
	 *               transform).  Locked components are pinned at their given
	 *               positions throughout the physics solve and overlap removal;
	 *               all other components are free.
	 */
	void place(Components& comps,
			   const NetList& nl,
			   const std::map<std::string, bend::Placement>& locked = {}) const {
		if(comps.empty()) return;

		// 1. Degree: number of signal-net connections per component.
		const auto compDegree = computeDegrees(comps, nl);

		// 2. Gravity-center component: the one with the most connections.
		const std::string centerComp = findCenter(comps, compDegree);

		// 3. Build libvpsc rectangles (one per component).
		//    Locked components start at their target positions; free components
		//    get the usual stagger initial state.
		//    Rectangle dimensions account for the component's transform: a 90°
		//    or 270° rotation swaps w and h in world space.
		// colaLocks is filled by buildRectangles — it has access to both the
		// locked world position and the rectangle dimensions needed for the
		// cola::Lock centre-coordinate calculation.
		std::vector<vpsc::Rectangle*> rs;
		std::map<std::string, unsigned> compId;
		std::vector<std::string> idToComp;
		std::vector<cola::Lock> colaLocks;
		buildRectangles(comps, compDegree, locked, rs, compId, idToComp, colaLocks);

		// 4. Build libcola spring edges from signal nets.
		std::vector<cola::Edge> edges;
		buildEdges(nl, compId, compDegree, centerComp, edges);

		// 5. Run physics solver with locks, then remove overlaps.
		//
		// Locked components are held fixed during the physics solve via
		// cola::PreIteration.  removeoverlaps() has no fixed-rectangle API,
		// so we run it on free components ONLY — locked ones are excluded.

		if(!colaLocks.empty()) {
			cola::PreIteration preIt(colaLocks);
			cola::ConstrainedFDLayout alg(rs, edges, opts.idealEdgeLength,
										  cola::StandardEdgeLengths, nullptr, &preIt);
			alg.run(true, true);
		} else {
			// // Add more constraints for initial layout (not working)
			// std::vector<cola::CompoundConstraint*> ccs;
			// buildConstraints(comps, nl, compId, compDegree, locked, ccs);
			// cola::ConstrainedFDLayout alg(rs, edges, opts_.idealEdgeLength);
			// if(!ccs.empty()) alg.setConstraints(ccs);
			// // alg.setUseNeighbourStress(true); // layout too condenced, errors when routing
			// alg.run(true, true);
			// for(auto* cc: ccs) delete cc;

			// Original code, no additional constraints
			cola::ConstrainedFDLayout alg(rs, edges, opts.idealEdgeLength);
			// alg.setUseNeighbourStress(true); // layout too condenced, errors when routing
			alg.run();
		}

		if(!locked.empty()) {
			// Remove overlaps among free components only.
			std::set<unsigned> fixedIdx; // index into rs
			for(unsigned i = 0; i < rs.size(); ++i) {
				if(locked.count(idToComp[i])) {
					fixedIdx.insert(i);
				}
			}
			removeoverlaps(rs, fixedIdx);
		} else {
			removeoverlaps(rs);
		}

		// 6. Normalise: shift so the top-left FREE component lands at canvasOrigin.
		applyResults(rs, idToComp, comps, locked);
	}

	/**
	 * Get the placement options in use.
	 * @return placement options
	 */
	const PlacementOptions& options() const {
		return opts;
	}

private:
	// ── Steps ─────────────────────────────────────────────────────────────────

	/**
	 * Step 1: degree map.
	 */
	std::map<std::string, int> computeDegrees(const Components& comps,
											  const NetList& nl) const {
		std::map<std::string, int> deg;
		// Pre-populate with zero so every component has an entry.
		for(const auto& [name, _]: comps) deg[name] = 0;

		for(const auto& netName: nl.netNames()) {
			if(opts.excludeBusesFromEdges && nl.isBus(netName)) continue;
			for(const auto& p: nl.pins(netName)) deg[p.comp]++;
		}
		return deg;
	}

	/**
	 * Step 2: gravity center.
	 */
	std::string findCenter(const Components& comps,
						   const std::map<std::string, int>& deg) const {
		std::string center = comps.begin()->first;
		int maxDeg = -1;
		for(const auto& [name, _]: comps) {
			if(deg.at(name) > maxDeg) {
				maxDeg = deg.at(name);
				center = name;
			}
		}
		return center;
	}

	/**
	 * Step 3: rectangles.
	 * Build rectangles AND fills colaLocks, since only here do we have both
	 * the locked world position and the rectangle dimensions needed to compute
	 * the rectangle centre (which is what cola::Lock requires).
	 *
	 * Rectangle dimensions are derived from effectiveDims(), which swaps w/h
	 * for components rotated 90° or 270°.  For locked components the transform
	 * comes from the supplied bend::Placement; for free components it comes
	 * from comp.placement (their current/last-known transform).
	 */
	void buildRectangles(const Components& comps,
						 const std::map<std::string, int>& deg,
						 const std::map<std::string, bend::Placement>& locked,
						 std::vector<vpsc::Rectangle*>& rs,
						 std::map<std::string, unsigned>& compId,
						 std::vector<std::string>& idToComp,
						 std::vector<cola::Lock>& colaLocks) const {
		unsigned id = 0;
		for(const auto& [name, comp]: comps) {
			const unsigned idx = id++;
			compId[name] = idx;
			idToComp.push_back(name);

			const double gap = (deg.at(name) == 0) ? opts.isolatedGap
												   : opts.routingGap;
			const double pad = opts.componentPad;

			// Use the locked placement's transform if the component is locked,
			// otherwise use the component's own current transform.
			// This ensures the rectangle dimensions match the actual world-space
			// footprint the component will occupy.
			const auto lockIt = locked.find(name);
			const bend::Transform& transform = lockIt != locked.end() ? lockIt->second.transform : comp.placement.transform;
			double cw = comp.w;
			double ch = comp.h;
			if(transform.isQuarterRotation()) {
				cw = comp.h;
				ch = comp.w;
			}

			const double w = cw + pad * 2.0 + gap;
			const double h = ch + pad * 2.0 + gap;

			double sx, sy;
			if(lockIt != locked.end()) {
				// Locked component: the locked position is the world-space center.
				// Compute the top-left of the padded rectangle from it.
				const double cx = lockIt->second.position.x;
				const double cy = lockIt->second.position.y;
				sx = cx - w / 2.0;
				sy = cy - h / 2.0;

				colaLocks.push_back(cola::Lock(idx, cx, cy));
			} else {
				// Free component: stagger initial position.
				sx = idx * opts.initialStagger;
				sy = idx * opts.initialStagger;
			}
			rs.push_back(new vpsc::Rectangle(sx, sx + w, sy, sy + h));
		}
	}

	/**
	 * Step 4: edges.
	 */
	void buildEdges(const NetList& nl,
					const std::map<std::string, unsigned>& compId,
					const std::map<std::string, int>& deg,
					const std::string& centerComp,
					std::vector<cola::Edge>& edges) const {
		// Signal-net edges: fully-connected clique per net (all pin pairs).
		for(const auto& netName: nl.netNames()) {
			if(opts.excludeBusesFromEdges && nl.isBus(netName)) continue;
			const auto& pins = nl.pins(netName);
			for(size_t i = 0; i < pins.size(); ++i)
				for(size_t j = i + 1; j < pins.size(); ++j)
					edges.push_back({compId.at(pins[i].comp),
									 compId.at(pins[j].comp)});
		}

		// Gravity tethers: connect isolated components to the center so they
		// don't drift to infinity during the physics solve.
		if(opts.addGravityTethers) {
			for(const auto& [name, d]: deg) {
				if(d == 0 && name != centerComp)
					edges.push_back({compId.at(name), compId.at(centerComp)});
			}
		}
	}

	/**
	 * Step 5+6: apply results.
	 *
	 * For every component the rectangle centre produced by libcola/libvpsc is
	 * written back as comp.placement.position (x, y).  The transform stored in
	 * comp.placement is never touched — the optimizer only moves components, it
	 * does not rotate or flip them.
	 *
	 * Locked components: their position is restored from the locked map rather
	 * than taken from the rectangle, so floating-point drift during the physics
	 * solve cannot corrupt a manually-placed component's exact coordinates.
	 */
	void applyResults(std::vector<vpsc::Rectangle*>& rs,
					  const std::vector<std::string>& idToComp,
					  Components& comps,
					  const std::map<std::string, bend::Placement>& locked) const {
		// Compute normalisation offset from FREE components only.
		// Locked components must not influence the offset — their positions
		// are absolute and must be preserved exactly.
		double minX = 1e9, minY = 1e9;
		for(size_t i = 0; i < rs.size(); ++i) {
			if(locked.count(idToComp[i])) continue; // skip locked
			if(rs[i]->getMinX() < minX) minX = rs[i]->getMinX();
			if(rs[i]->getMinY() < minY) minY = rs[i]->getMinY();
		}
		// Fallback: if ALL components are locked, no normalisation needed.
		const double offsetX = (minX < 1e9) ? opts.canvasOrigin - minX : 0.0;
		const double offsetY = (minY < 1e9) ? opts.canvasOrigin - minY : 0.0;

		for(size_t i = 0; i < rs.size(); ++i) {
			const std::string& name = idToComp[i];
			const vpsc::Rectangle* rect = rs[i];
			Component& comp = comps[name];

			// The center of the libvpsc rectangle perfectly matches the
			// center of the component, as padding is symmetrical.
			comp.placement.position.x = rect->getCentreX() + offsetX;
			comp.placement.position.y = rect->getCentreY() + offsetY;
			const auto lockIt = locked.find(name);
			if(lockIt != locked.end()) {
				comp.placement.transform = lockIt->second.transform;
			}

			delete rs[i];
		}
	}

	/**
	 * Build libcola compound constraints that bias small graphs toward a single
	 * horizontal row, and larger graphs toward stacked horizontal layers.
	 *
	 * Locked components are skipped here because their positions are already
	 * enforced elsewhere by cola::Lock in the PreIteration step.
	 */
	void buildConstraints(const Components& comps,
						  const NetList& nl,
						  const std::map<std::string, unsigned>& compId,
						  const std::map<std::string, int>& deg,
						  const std::map<std::string, bend::Placement>& locked,
						  std::vector<cola::CompoundConstraint*>& ccs) const {
		if(comps.empty()) return;

		auto isLocked = [&](const std::string& name) -> bool {
			return locked.find(name) != locked.end();
		};

		auto getBusLevel = [&](const std::string& netName) -> int {
			return nl.busLevel(netName);
		};

		// Graph size heuristic.
		const bool smallGraph = comps.size() <= 10;

		// These are the main tuning knobs for the constraint scaffold.
		const double rowGap = std::max(60.0, opts.idealEdgeLength * (smallGraph ? 0.90 : 0.75));
		const double colGap = std::max(35.0, opts.idealEdgeLength * (smallGraph ? 0.60 : 0.45));

		const size_t n = comps.size();
		const size_t signalEdgeCount = std::accumulate(nl.getRawNets().begin(), nl.getRawNets().end(), size_t{0},
													   [&](size_t sum, const auto& pair) {
														   const auto& [key, vec] = pair;
														   return sum + (nl.isSignal(key) ? vec.size() : 0);
													   });
		const bool simpleGraph = (n <= 2) || (signalEdgeCount <= 2);

		if(simpleGraph) {
			auto* align = new cola::AlignmentConstraint(vpsc::YDIM);
			for(const auto& [name, comp]: comps) {
				(void)comp;
				if(locked.count(name)) continue;
				align->addShape(compId.at(name), 0.0);
			}
			ccs.push_back(align);

			// Optional: keep a left-to-right order.
			std::vector<std::string> names;
			for(const auto& [name, comp]: comps) {
				(void)comp;
				if(!locked.count(name)) names.push_back(name);
			}
			std::stable_sort(names.begin(), names.end(),
							 [&](const std::string& a, const std::string& b) {
								 const int da = deg.at(a);
								 const int db = deg.at(b);
								 if(da != db) return da > db;
								 return a < b;
							 });

			for(size_t i = 1; i < names.size(); ++i) {
				ccs.push_back(new cola::SeparationConstraint(
						vpsc::XDIM,
						compId.at(names[i - 1]),
						compId.at(names[i]),
						colGap,
						false));
			}

			return;
		}
	}

	PlacementOptions opts;
};
