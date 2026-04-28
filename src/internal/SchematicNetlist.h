/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once
// internal/SchematicNetlist.h
// Plain storage for the netlist description. No adaptagrams types.

#include "../WireBenderTypes.h"
#include <vector>

namespace WireBender {

struct SchematicNetlist {
	const ComponentDescriptor* findComponent(const std::string& id) const {
		for (const auto& c : components)
			if (c.id == id) return &c;
		return nullptr;
	}

	const NetDescriptor* findNet(const std::string& name) const {
		for (const auto& n : nets)
			if (n.name == name) return &n;
		return nullptr;
	}

	std::vector<ComponentDescriptor> components;
	std::vector<NetDescriptor>		 nets;
};

} // namespace WireBender
