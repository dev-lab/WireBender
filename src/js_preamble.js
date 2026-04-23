/**
 * @file WireBender.js
 * @description WireBender — schematic routing and PCB net visualisation library.
 *
 * Compiled from C++ via Emscripten. Load as an ES6 module:
 *
 * @example
 * import WireBenderModule from './WireBender.js';
 *
 * const Module = await WireBenderModule();
 *
 * // ── Schematic routing ────────────────────────────────────────────────────
 * const wb = new Module.WireBender();
 *
 * // Component origin (0,0) is the center of its bounding box.
 * wb.addComponent({ id: 'U1', width: 80, height: 60, padding: 16, pins:[
 *	 { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: Module.PinDirection.DirUp },
 *	 { number: 2, name: 'GND', x:  20, y:  30, directionFlags: Module.PinDirection.DirDown },
 *	 { number: 3, name: 'IN',  x: -40, y:	0, directionFlags: Module.PinDirection.DirLeft },
 *	 { number: 4, name: 'OUT', x:  40, y:	0, directionFlags: Module.PinDirection.DirRight },
 * ]});
 *
 * wb.addNet({ name: 'VCC', pins:[{ componentId: 'U1', pinNumber: 1 }, ...] });
 *
 * const classification = wb.classify();	  // auto-detect buses
 * wb.applyClassification(classification);	  // or modify first
 *
 * const placements = wb.computePlacements(); // automatic layout
 * const routes	   = wb.routeAll();			  // route all nets
 *
 * // During interactive drag:
 * const delta = wb.moveComponent('U1', {position: { x: 200, y: 150 }, transform: {rotation: 0, flipX: false}});
 * // delta.affectedNets — which nets changed
 * // delta.routes.wires — updated wire polylines for those nets
 *
 * // ── PCB pad visualisation ────────────────────────────────────────────────
 * const pcb = new Module.PcbVisualizer();
 * pcb.addNet({ name: 'VCC', pads:[{ x: 10, y: 20 }, { x: 50, y: 80 }] });
 * const pcbRoutes = pcb.route();
 */

// ─────────────────────────────────────────────────────────────────────────────
// Type definitions (for IDE autocompletion in plain JS projects)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A 2-D point in world coordinates.
 * @typedef	 {Object} Point2D
 * @property {number} x
 * @property {number} y
 */

/**
 * Transform state.
 * @typedef	 {Object} Transform
 * @property {number} rotation
 * @property {boolean} flipX
 */

/**
 * Pin connection direction hints. Combine with bitwise OR.
 * @enum {number}
 */
// Module.PinDirection.DirNone | DirUp | DirDown | DirLeft | DirRight | DirAll

/**
 * Describes a single pin on a component.
 * @typedef	 {Object} PinDescriptor
 * @property {number} number		 - Integer pin identity (stable across renames).
 * @property {string} name			 - Human label; may be empty.
 * @property {number} x				 - X position in component-local coordinates (0 = center).
 * @property {number} y				 - Y position in component-local coordinates (0 = center).
 * @property {number} directionFlags - Preferred wire exit directions (PinDirection flags).
 */

/**
 * Describes a component's geometry and pins.
 * @typedef	 {Object} ComponentDescriptor
 * @property {string}		   id	   - Unique identifier, e.g. "U1".
 * @property {number}		   width
 * @property {number}		   height
 * @property {number}		   padding - Routing clearance around the bounding box.
 * @property {PinDescriptor[]} pins
 */

/**
 * Component placement.
 * @typedef	 {Object} Placement
 * @property {Point2D} 		position
 * @property {Transform} 	transform
 */

/**
 * References a specific pin on a specific component.
 * @typedef	 {Object} PinRef
 * @property {string} componentId
 * @property {number} pinNumber
 */

/**
 * Describes an electrical net connecting one or more pins.
 * @typedef	 {Object} NetDescriptor
 * @property {string}	name
 * @property {PinRef[]} pins
 */

/**
 * Classification of a net produced by {@link WireBender#classify}.
 * All fields may be modified before passing back to
 * {@link WireBender#applyClassification}.
 * @typedef	 {Object}  NetClassification
 * @property {string}  name
 * @property {boolean} isBus	  - True if the net is a power/ground bus rail.
 * @property {boolean} isGround	  - True if this is the ground reference (drawn below components).
 * @property {boolean} isPositive - True if this is a positive power rail (drawn above components).
 * @property {number}  busLevel	  - Vertical ordering among buses (0 = topmost). -1 for signals.
 */

/**
 * Component placements container, used in {@link WireBender#computePlacements}, {@link WireBender#setLockedPlaceemnts}.
 * @typedef	 {Object} ComponentPlacements
 * @property {(id: string, placement: Placement) => void} set		- set placement for component ID
 * @property {(id: string) => Placement} get						- returns placement for component ID
 * @property {() => Object<string, Placement>} toObject				- Converts to plain JS object.
 * @property {(obj: Object<string, Placement>) => void} fromObject	- Replaces contents from plain JS object.
 */

/**
 * A single routed wire polyline. Points are already snapped to junction centres.
 * @typedef	 {Object}	 Wire
 * @property {string}	 net	- Net name.
 * @property {Point2D[]} points - Ordered polyline vertices.
 */

/**
 * A T-junction dot where three or more wires meet.
 * @typedef	 {Object}  JunctionDot
 * @property {string}  net
 * @property {Point2D} position
 */

/**
 * Suggested anchor position for a net label.
 * @typedef	 {Object}  NetLabelHint
 * @property {string}  net
 * @property {Point2D} position		- Near the midpoint of the longest wire segment.
 * @property {boolean} isVertical	- Is wire segment vertical at this point?
 */

/**
 * Suggested text anchor positions for the two labels drawn beside a schematic
 * symbol: the reference designator (e.g. "R135") and the value (e.g. "68k").
 *
 * The library picks the first candidate position (above / below / right / left
 * of the component bounding box) that does not overlap any wire segment or
 * other component, so the caller can render the labels at the given coordinates
 * without a separate collision pass.
 *
 * `isVertical` is set when text shall be rotated to 90 degrees
 *
 * Both positions are world-space centre anchors for the text.
 *
 * @typedef	 {Object}  ComponentLabelHint
 * @property {string}  componentId
 * @property {Point2D} refPosition		 - Centre anchor for the reference designator.
 * @property {boolean} refIsVertical     - Show ref label vertically.
 * @property {Point2D} valuePosition     - Centre anchor for the value / part-number label.
 * @property {boolean} valueIsVertical   - Show value label vertically.
 */

/**
 * Full routing result from {@link WireBender#routeAll}.
 * @typedef	 {Object}		 SchematicRouteResult
 * @property {Wire[]}		 wires
 * @property {JunctionDot[]} junctions
 * @property {NetLabelHint[]}	 netLabels			- One entry per net: suggested net-name label position.
 * @property {ComponentLabelHint[]} componentLabels - One entry per component: suggested ref and value positions.
 */

/**
 * Partial routing result from {@link WireBender#moveComponent}.
 * Contains only the nets connected to the moved component.
 * `routes.componentLabels` contains exactly one entry — the updated hint for
 * the moved component.	 Merge it into the full result by replacing the entry
 * whose `componentId` matches.
 * @typedef	 {Object}				IncrementalRouteResult
 * @property {string[]}				affectedNets - Names of re-routed nets.
 * @property {SchematicRouteResult} routes		 - Updated wires, junctions, net labels,
 *												   and the moved component's label hint.
 */

/**
 * Component replacement pin map.
 * @typedef	 {Object} PinMap
 * @property {(oldPin: number, newPin: number) => void} set		- set pin numbers mapping
 * @property {(oldPin: number) => number} get					- returns replacement pin number
 * @property {() => Object<number, number>} toObject 			- Converts to plain JS object.
 * @property {(obj: Object<number, number>) => void} fromObject	- Replaces contents from plain JS object.
 */

/**
 * Component replacement (e.g. 3 pin generic rect component with transistor).
 * @typedef {Object} ComponentReplacement
 * @property {string} componentId					- component ID
 * @property {ComponentDescriptor} newDescriptor	- new component descriptor
 * @property {PinMap} pinMapping					- pin mapping: old to new
 */

/**
 * A PCB net for {@link PcbVisualizer}.
 * @typedef	 {Object}	 PcbNet
 * @property {string}	 name
 * @property {Point2D[]} pads - Physical pad world-positions on the PCB photo.
 */

/**
 * Routing result from {@link PcbVisualizer#route}.
 * @typedef	 {Object}		 PcbRouteResult
 * @property {Wire[]}		 wires
 * @property {JunctionDot[]} junctions
 */

// ─────────────────────────────────────────────────────────────────────────────
// Class documentation
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Main entry point for schematic diagram routing.
 *
 * Workflow:
 * 1. {@link WireBender#addComponent} / {@link WireBender#addNet} — describe the netlist.
 * 2. {@link WireBender#classify} + {@link WireBender#applyClassification} — net classification.
 * 3. {@link WireBender#setLockedPlacement} + {@link WireBender#computePlacement} + {@link WireBender#setPlacements} — automatic component placement.
 * 4. {@link WireBender#routeAll} — route all nets.
 * 5. {@link WireBender#moveComponent} — incremental re-route during interactive drag/rotation.
 * 6. {@link WireBender#replaceComponent} — swap component geometry (e.g. library resolution).
 *
 * @class WireBender
 */

/**
 * Routes pad clusters on a PCB photo as optimized multi-point networks.
 * No component obstacles. Non-orthogonal routing. Crossing minimisation.
 *
 * @class PcbVisualizer
 */
