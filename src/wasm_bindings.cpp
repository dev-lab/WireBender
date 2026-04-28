/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#include "PcbVisualizer.h"
#include "WireBender.h"
#include <emscripten/bind.h>
#include <emscripten/val.h>

// Avoid the ambiguity between the WireBender namespace and the WireBender class
namespace WB = WireBender;
using namespace emscripten;

/**
 * Emscripten bindings for WireBender and PcbVisualizer.
 * No logic here — pure binding declarations.
 */
EMSCRIPTEN_BINDINGS(wirebender) {

	value_object<WB::Point2D>("Point2D")
			.field("x", &WB::Point2D::x)
			.field("y", &WB::Point2D::y);

	value_object<WB::Transform>("Transform")
			.field("rotation", &WB::Transform::rotation)
			.field("flipX", &WB::Transform::flipX);

	enum_<WB::PinDirection>("PinDirection")
			.value("DirNone", WB::DirNone)
			.value("DirUp", WB::DirUp)
			.value("DirDown", WB::DirDown)
			.value("DirLeft", WB::DirLeft)
			.value("DirRight", WB::DirRight)
			.value("DirAll", WB::DirAll);

	value_object<WB::PinDescriptor>("PinDescriptor")
			.field("number", &WB::PinDescriptor::number)
			.field("name", &WB::PinDescriptor::name)
			.field("x", &WB::PinDescriptor::x)
			.field("y", &WB::PinDescriptor::y)
			.field("directionFlags", &WB::PinDescriptor::directionFlags);

	value_object<WB::ComponentDescriptor>("ComponentDescriptor")
			.field("id", &WB::ComponentDescriptor::id)
			.field("width", &WB::ComponentDescriptor::width)
			.field("height", &WB::ComponentDescriptor::height)
			.field("padding", &WB::ComponentDescriptor::padding)
			.field("pins", &WB::ComponentDescriptor::pins);

	value_object<WB::Placement>("Placement")
			.field("position", &WB::Placement::position)
			.field("transform", &WB::Placement::transform);

	value_object<WB::PinRef>("PinRef")
			.field("componentId", &WB::PinRef::componentId)
			.field("pinNumber", &WB::PinRef::pinNumber);

	value_object<WB::NetDescriptor>("NetDescriptor")
			.field("name", &WB::NetDescriptor::name)
			.field("pins", &WB::NetDescriptor::pins);

	value_object<WB::NetClassification>("NetClassification")
			.field("name", &WB::NetClassification::name)
			.field("isBus", &WB::NetClassification::isBus)
			.field("isGround", &WB::NetClassification::isGround)
			.field("isPositive", &WB::NetClassification::isPositive)
			.field("busLevel", &WB::NetClassification::busLevel);

	class_<WB::ComponentPlacements>("ComponentPlacements")
			.constructor<>()
			.function("set", optional_override([](WB::ComponentPlacements& self, const std::string& id, const WB::Placement& p) {
						  self.placements[id] = p;
					  }))
			.function("get", optional_override([](const WB::ComponentPlacements& self, const std::string& id) {
						  auto it = self.placements.find(id);
						  return it != self.placements.end() ? it->second : WB::Placement();
					  }))
			.function("toObject", optional_override([](const WB::ComponentPlacements& self) {
						  val obj = val::object();
						  for(const auto& [k, v]: self.placements) {
							  obj.set(k, v);
						  }
						  return obj;
					  }))
			.function("fromObject", optional_override([](WB::ComponentPlacements& self, val obj) {
						  self.placements.clear();
						  val keys = val::global("Object").call<val>("keys", obj);
						  int len = keys["length"].as<int>();
						  for(int i = 0; i < len; i++) {
							  std::string k = keys[i].as<std::string>();
							  self.placements[k] = obj[k].as<WB::Placement>();
						  }
					  }));

	value_object<WB::Wire>("Wire")
			.field("net", &WB::Wire::net)
			.field("points", &WB::Wire::points);

	value_object<WB::JunctionDot>("JunctionDot")
			.field("net", &WB::JunctionDot::net)
			.field("position", &WB::JunctionDot::position);

	value_object<WB::NetLabelHint>("NetLabelHint")
			.field("net", &WB::NetLabelHint::net)
			.field("position", &WB::NetLabelHint::position)
			.field("isVertical", &WB::NetLabelHint::isVertical);

	value_object<WB::ComponentLabelHint>("ComponentLabelHint")
			.field("componentId", &WB::ComponentLabelHint::componentId)
			.field("refPosition", &WB::ComponentLabelHint::refPosition)
			.field("refIsVertical", &WB::ComponentLabelHint::refIsVertical)
			.field("valuePosition", &WB::ComponentLabelHint::valuePosition)
			.field("valueIsVertical", &WB::ComponentLabelHint::valueIsVertical);

	value_object<WB::SchematicRouteResult>("SchematicRouteResult")
			.field("wires", &WB::SchematicRouteResult::wires)
			.field("junctions", &WB::SchematicRouteResult::junctions)
			.field("netLabels", &WB::SchematicRouteResult::netLabels)
			.field("componentLabels", &WB::SchematicRouteResult::componentLabels);

	value_object<WB::IncrementalRouteResult>("IncrementalRouteResult")
			.field("affectedNets", &WB::IncrementalRouteResult::affectedNets)
			.field("routes", &WB::IncrementalRouteResult::routes);

	class_<WB::PinMap>("PinMap")
			.constructor<>()
			.function("set", optional_override([](WB::PinMap& self, int oldPin, int newPin) {
						  self.oldToNew[oldPin] = newPin;
					  }))
			.function("get", optional_override([](const WB::PinMap& self, int oldPin) {
						  auto it = self.oldToNew.find(oldPin);
						  return it != self.oldToNew.end() ? it->second : -1;
					  }))
			.function("toObject", optional_override([](const WB::PinMap& self) {
						  val obj = val::object();
						  for(const auto& [k, v]: self.oldToNew) {
							  obj.set(k, v);
						  }
						  return obj;
					  }))
			.function("fromObject", optional_override([](WB::PinMap& self, val obj) {
						  self.oldToNew.clear();
						  val keys = val::global("Object").call<val>("keys", obj);
						  int len = keys["length"].as<int>();
						  for(int i = 0; i < len; i++) {
							  int k = keys[i].as<int>();
							  self.oldToNew[k] = obj[k].as<int>();
						  }
					  }));

	value_object<WB::ComponentReplacement>("ComponentReplacement")
			.field("componentId", &WB::ComponentReplacement::componentId)
			.field("newDescriptor", &WB::ComponentReplacement::newDescriptor)
			.field("pinMapping", &WB::ComponentReplacement::pinMapping);

	value_object<WB::PcbNet>("PcbNet")
			.field("name", &WB::PcbNet::name)
			.field("pads", &WB::PcbNet::pads);

	value_object<WB::PcbRouteResult>("PcbRouteResult")
			.field("wires", &WB::PcbRouteResult::wires)
			.field("junctions", &WB::PcbRouteResult::junctions);

	register_vector<WB::Point2D>("VectorPoint2D");
	register_vector<WB::PinDescriptor>("VectorPinDescriptor");
	register_vector<WB::PinRef>("VectorPinRef");
	register_vector<WB::Wire>("VectorWire");
	register_vector<WB::JunctionDot>("VectorJunctionDot");
	register_vector<WB::NetLabelHint>("VectorNetLabelHint");
	register_vector<WB::ComponentLabelHint>("VectorComponentLabelHint");
	register_vector<WB::NetClassification>("VectorNetClassification");
	register_vector<std::string>("VectorString");
	register_vector<WB::PcbNet>("VectorPcbNet");

	class_<WB::WireBender>("WireBender")
			.constructor<>()
			.function("addComponent", &WB::WireBender::addComponent)
			.function("addNet", &WB::WireBender::addNet)
			.function("clear", &WB::WireBender::clear)
			.function("classify", &WB::WireBender::classify)
			.function("applyClassification", &WB::WireBender::applyClassification)
			.function("setLockedPlacements", &WB::WireBender::setLockedPlacements)
			.function("computePlacements", &WB::WireBender::computePlacements)
			.function("setComponentPlacement", &WB::WireBender::setComponentPlacement)
			.function("setPlacements", &WB::WireBender::setPlacements)
			.function("routeAll", &WB::WireBender::routeAll)
			.function("moveComponent", &WB::WireBender::moveComponent)
			.function("replaceComponent", &WB::WireBender::replaceComponent)
			.function("printRoutingStats", &WB::WireBender::printRoutingStats);

	class_<WB::PcbVisualizer>("PcbVisualizer")
			.constructor<>()
			.function("addNet", &WB::PcbVisualizer::addNet)
			.function("clear", &WB::PcbVisualizer::clear)
			.function("route", &WB::PcbVisualizer::route);
}
