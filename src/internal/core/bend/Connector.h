/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Junction.h"

namespace bend {

/**
 * @brief Thin wrapper around Avoid::ConnRef*
 * Non-owning by default.
 */
class Connector {
public:
	/**
	 * @brief Construct from raw pointer
	 * @param connector pointer to Avoid::ConnRef (non-owning)
	 */
	explicit Connector(Avoid::ConnRef* connector)
		: connector(connector) {
		init();
	}

	/**
	 * @brief Default constructor (null connector)
	 */
	Connector() noexcept = default;

	/**
	 * @brief Get underlying pointer
	 */
	Avoid::ConnRef* get() const noexcept {
		return connector;
	}

	/**
	 * @brief Check if valid
	 */
	bool isValid() const noexcept {
		return connector != nullptr;
	}

	/**
	 * @brief Implicit bool conversion
	 */
	explicit operator bool() const noexcept {
		return isValid();
	}

	/**
	 * @brief Equality operators (pointer identity)
	 */
	bool operator==(const Connector& other) const noexcept {
		return connector == other.connector;
	}

	bool operator!=(const Connector& other) const noexcept {
		return !(*this == other);
	}

	Avoid::ConnRef* operator->() const noexcept {
		return connector;
	}

	/**
	 * @brief Get Id
	 */
	unsigned int getId() const noexcept {
		return isValid() ? get()->id() : 0u;
	}

	/**
	 * @brief get endpoints
	 */
	inline std::pair<Avoid::ConnEnd, Avoid::ConnEnd> endpoints() const {
		return isValid() ? get()->endpointConnEnds() : std::make_pair(Avoid::ConnEnd(), Avoid::ConnEnd());
	}

	/**
	 * @brief get endpoint junctions
	 */
	inline std::pair<Junction, Junction> junctions() const {
		if(!isValid()) return {Junction(), Junction()};
		auto ends = endpoints();
		return {Junction(ends.first), Junction(ends.second)};
	}

	/**
	 * @brief get route points
	 */
	const std::vector<Avoid::Point>& getPoints() const {
		if(isValid()) {
			return get()->displayRoute().ps;
		}
		static const std::vector<Avoid::Point> empty;
		return empty;
	}

	size_t size() const {
		return isValid() ? get()->displayRoute().size() : 0u;
	}

private:
	inline void init() {
		if(connector) connector->displayRoute(); // updates connector route
	}

	Avoid::ConnRef* connector = nullptr;
};
} // namespace bend