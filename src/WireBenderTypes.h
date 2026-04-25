#pragma once

// All public data structures exchanged between WireBender and its callers.

#include <map>
#include <string>
#include <vector>

namespace WireBender {

/**
 * Point.
 */
struct Point2D {
	double x = 0.0;
	double y = 0.0;
};

/**
 * Component transform state.
 */
struct Transform {
	int rotation; // rotation in degrees 0, 90, 180, 270
	bool flipX;	  // flip X
};

/**
 * Direction flags for pin connection hints.
 * Values match internal flags so the implementation can cast directly,
 * but callers treat them as opaque bit flags or use the named constants below.
 */
enum PinDirection : int {
	DirNone = 0,
	DirUp = 1,
	DirDown = 2,
	DirLeft = 4,
	DirRight = 8,
	DirAll = 15
};

/**
 * Component pin descriptor.
 */
struct PinDescriptor {
	int number;		  // immutable integer identity used for net connections and pin mapping
	std::string name; // optional label ("Gate", "VCC", ""); may be ""
	double x = 0.0;				 // x position in component-local coordinates: 0 - center of the component box
	double y = 0.0;				 // y position in component-local coordinates: 0 - center of the component box
	int directionFlags = DirAll; // pin direction flags
};

/**
 * Component descriptor.
 */
struct ComponentDescriptor {
	std::string id;					 // unique ID, e.g. "U1", "R3"
	double width = 0.0;				 // component width
	double height = 0.0;			 // component height
	double padding = 16.0;			 // routing clearance around the box
	std::vector<PinDescriptor> pins; // component pins
};

/**
 * Placement: position and orientation in world space (of the component)
 */
struct Placement {
	Point2D position;				  // center world pos
	Transform transform = {0, false}; // rotation/flip
};

/**
 * Component pin reference.
 */
struct PinRef {
	std::string componentId; // component ID
	int pinNumber;			 // pin number
};

/**
 * Net definition (connected component pins).
 */
struct NetDescriptor {
	std::string name;		  // net name
	std::vector<PinRef> pins; // net component pins
};

/**
 * Net Classification: WireBender produces; caller may modify and return.
 */
struct NetClassification {
	std::string name;		 // net name
	bool isBus = false;		 // true if net is a bus (e.g. VCC, GND)
	bool isGround = false;	 // drawn below components
	bool isPositive = false; // drawn above components
	int busLevel = -1;		 // ordering among buses (0 = topmost)
};

/**
 * Component placements.
 */
struct ComponentPlacements {
	std::map<std::string, Placement> placements; // componentId -> component placement
};

/**
 * Wire (routing output).
 */
struct Wire {
	std::string net;			 // net name
	std::vector<Point2D> points; // snapped polyline; every point is valid
};

/**
 * Junction (routing output).
 */
struct JunctionDot {
	std::string net;  // net name
	Point2D position; // position
};

/**
 * Net label hint (routing output).
 */
struct NetLabelHint {
	std::string net;  // net name
	Point2D position; // suggested anchor near midpoint of longest segment
	bool isVertical;  // true if wire segment goes vertical, horizontal otherwise
};

/**
 * Suggested positions for the two text labels drawn beside a schematic symbol:
 * - ref - the reference designator, e.g. "R135", "U3";
 * - value - the component value / part number, e.g. "68k", "LM358".
 * Both positions are world-coordinate centres of the text anchor.
 * isVerticalOnly is set when only the space above / below the component is
 * unobstructed; the caller should orient or abbreviate the label accordingly
 * (mirrors the same flag on LabelHint used for net names).
 */
struct ComponentLabelHint {
	std::string componentId; // component ID
	Point2D refPosition;	 // centre anchor for the reference label
	bool refIsVertical;		 // true if ref label shall be printed vertically
	Point2D valuePosition;	 // centre anchor for the value label
	bool valueIsVertical;	 // true if value label shall be printed vertically
};

/**
 * Schematic diagram routing result.
 */
struct SchematicRouteResult {
	std::vector<Wire> wires;						 // wires
	std::vector<JunctionDot> junctions;				 // junctions
	std::vector<NetLabelHint> netLabels;			 // net label positions
	std::vector<ComponentLabelHint> componentLabels; // component ref/value labels positions
};

/**
 * Partial routing (e.g. if component is replaced).
 */
struct IncrementalRouteResult {
	std::vector<std::string> affectedNets; // affected net names
	SchematicRouteResult routes;		   // routing data
};

/**
 * Component replacement pin map.
 */
struct PinMap {
	std::map<int, int> oldToNew; // oldPinNumber -> newPinNumber absent keys = pin removed
};

/**
 * Component replacement (e.g. 3 pin generic rect component with transistor).
 */
struct ComponentReplacement {
	std::string componentId;		   // component ID
	ComponentDescriptor newDescriptor; // new component descriptor
	PinMap pinMapping;				   // pin mapping: old to new
};

/**
 * PCB net description.
 */
struct PcbNet {
	std::string name;		   // net name
	std::vector<Point2D> pads; // pads belonging to the same net
};

/**
 * PCB net visualization.
 */
struct PcbRouteResult {
	std::vector<Wire> wires;			// net wires
	std::vector<JunctionDot> junctions; // net junctions
};

} // namespace WireBender
