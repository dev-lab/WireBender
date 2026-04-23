#pragma once
#include "WireBenderTypes.h"
#include <memory>

namespace WireBender {

class WireBenderImpl; // pimpl

/**
 * Primary public API for schematic diagram routing.
 *
 * Usage pattern:
 *	 WireBender::WireBender wb;
 *
 *	 // 1. Describe the netlist
 *	 wb.addComponent({...});
 *	 wb.addNet({...});
 *	 // 2. Classify nets (library guesses; caller can override)
 *	 auto cls = wb.classify();
 *	 cls[0].isBus = false;			 // override a classification
 *	 wb.applyClassification(cls);
 *
 *	 // 3. Place components
 *	 auto placement = wb.computePlacement();
 *	 // optionally adjust placement.positions, then:
 *	 wb.setComponentPlacement("U1", {{newX,newY},{newRotation,newFlipX}});
 *
 *	 // 4. Route
 *	 auto routes = wb.routeAll();
 *
 *	 // 5. Incremental re-route while user drags
 *	 auto delta = wb.moveComponent("U1", {{x, y},{rotation,flipX}});  // call repeatedly on drag
 *
 *	 // 6. Replace a component (e.g. resolved from KiCad library)
 *	 auto routes = wb.replaceComponent({...});
 */
class WireBender {
public:
	WireBender();
	~WireBender();

	/**
	 * Non-copyable: owns routing state that cannot be cheaply duplicated.
	 */
	WireBender(const WireBender&) = delete;

	/**
	 * Non-assignable: owns routing state that cannot be cheaply duplicated.
	 */
	WireBender& operator=(const WireBender&) = delete;

	/**
	 * Add a component.
	 * May be called multiple times before classify().
	 * Replaces any existing component with the same id.
	 * @param componentDescriptor component to be added
	 */
	void addComponent(const ComponentDescriptor& comp);

	/**
	 * Add a net. May be called multiple times.
	 * If a net with the same name already exists it is replaced.
	 * @param net net to be added
	 */
	void addNet(const NetDescriptor& net);

	/**
	 * Remove everything and return to the initial empty state.
	 */
	void clear();

	/**
	 * Auto-classify all nets (statistical bus detection + rail heuristics).
	 * @return one entry per net, in alphabetical order.
	 */
	std::vector<NetClassification> classify() const;

	/**
	 * Override the library's classification with the caller's version.
	 * Must be called before computePlacement() for overrides to take effect.
	 * @param cls net classification to be applied
	 */
	void applyClassification(const std::vector<NetClassification>& cls);

	/**
	 * Lock specific components at fixed placements before computePlacement().
	 * Locked components are held immovable by the solver; all others are
	 * placed around them. Call with an empty placements to clear all locks.
	 * Locks apply only to computePlacement() — they do not affect routeAll()
	 * or moveComponent().
	 * @param locks component locked placements
	 */
	void setLockedPlacements(const ComponentPlacements& locks);

	/**
	 * Run automatic placement using the current netlist and classification.
	 * Returns placements with suggested center positions for all components.
	 * The caller may accept the result as-is or adjust individual positions
	 * via setComponentPosition() before calling routeAll().
	 * @return position of all the components
	 */
	ComponentPlacements computePlacements();

	/**
	 * Override the placement of one component.
	 * Takes effect on the next routeAll() or moveComponent() call.
	 * @param componentId component ID to set placement
	 * @param placement new component placement
	 */
	void setComponentPlacement(const std::string& componentId, const Placement& placement);

	/**
	 * Override the placements for many components.
	 * Takes effect on the next routeAll() or moveComponent() call.
	 * @param placements component placements
	 */
	void setPlacements(const ComponentPlacements& placements);

	/**
	 * Route all nets using current component positions and classification.
	 * Must be called after computePlacement() or after setComponentPosition()
	 * calls that cover all components.
	 * @return schematic route result
	 */
	SchematicRouteResult routeAll();

	/**
	 * Update one component's placement and re-route only the nets connected to
	 * its pins.  Designed to be called repeatedly during interactive drag.
	 * Returns only the affected nets so the UI can merge the result into the
	 * previously returned full SchematicRouteResult.
	 * @param componentId component ID that was moved or rotated
	 * @param placement new component placement
	 * @return parial re-routing result
	 */
	IncrementalRouteResult moveComponent(const std::string& componentId, const Placement& placement);

	/**
	 * Replace a component's geometry (e.g. after resolving it from a KiCad
	 * library).  Applies the pin mapping, reconnects affected nets, then
	 * re-routes the full schematic.
	 * Nets connected to removed pins are silently disconnected.
	 * Nets connected to new pins must be added via addNet() before calling
	 * replaceComponent() if they are not already present.
	 * @param replacement component replacement with pin mapping
	 * @return schematic route result
	 */
	SchematicRouteResult replaceComponent(const ComponentReplacement& replacement);

	/**
	 * Prints diagnostic information about the current routing state to output
	 * Only valid after routeAll() or moveComponent().
	 * @return is routing stats ok?
	 */
	bool printRoutingStats() const;

private:
	std::unique_ptr<WireBenderImpl> impl;
};

} // namespace WireBender
