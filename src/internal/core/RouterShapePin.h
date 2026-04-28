/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "libavoid/libavoid.h"

/**
 * @brief Thin wrapper around Avoid::Shape* and its pin class ID.
 * Non-owning by default.
 */
class RouterShapePin {
public:
	/**
	 * @brief Construct from raw pointer
	 * @param shape pointer to Avoid::ShapeRef (non-owning)
	 * @param pinClassId pin class ID
	 */
	explicit RouterShapePin(Avoid::ShapeRef* shape, unsigned int pinClassId) noexcept
		: shape(shape), pinClassId(pinClassId) {}

	/**
	 * @brief Construct from raw pointer
	 * @param end connector end
	 */
	explicit RouterShapePin(const Avoid::ConnEnd& end)
		: RouterShapePin(end.type() == Avoid::ConnEndShapePin ? end.shape() : nullptr, end.pinClassId()) {
	}

	/**
	 * @brief Default constructor (null shape)
	 */
	RouterShapePin() noexcept = default;

	/**
	 * @brief Get underlying pointer
	 */
	Avoid::ShapeRef* get() const noexcept {
		return shape;
	}

	/**
	 * @brief Check if valid
	 */
	bool isValid() const noexcept {
		return shape != nullptr;
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
	bool operator==(const RouterShapePin& other) const noexcept {
		return shape == other.shape && pinClassId == other.pinClassId;
	}

	bool operator!=(const RouterShapePin& other) const noexcept {
		return !(*this == other);
	}

	Avoid::ShapeRef* operator->() const noexcept {
		return shape;
	}

	/**
	 * @brief Get Id
	 */
	unsigned int getId() const noexcept {
		return isValid() ? get()->id() : 0u;
	}

	/**
	 * @brief Get pin class Id
	 */
	unsigned int getPinClassId() const noexcept {
		return pinClassId;
	}

private:
	Avoid::ShapeRef* shape = nullptr;
	unsigned int pinClassId = 0u;
};
