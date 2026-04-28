/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

namespace bend {

/**
 * Placement transform (rotation, flip).
 */
class Transform {
public:
	/**
	 * @brief Empty transform.
	 */
	inline Transform() noexcept : rotation(0), flipX(false) {
	}

	/**
	 * @brief Create Transform.
	 * @param rotation rotation in degree, any integer
	 * @param flipX true if flipped horizontally
	 */
	inline explicit Transform(int rotation, bool flipX) noexcept : rotation(normalizeRotation(rotation)), flipX(flipX) {
	}

	/**
	 * @brief check if transform is empty
	 */
	inline bool isEmpty() const {
		return !(isRotation() || isFlipX());
	}

	/**
	 * @brief with rotation
	 */
	inline bool isRotation() const {
		return getRotation() > 0;
	}

	/**
	 * @brief get rotation (in degrees)
	 */
	inline int getRotation() const {
		return rotation;
	}

	/**
	 * @brief Is quarter-turn rotation.
	 */
	inline bool isQuarterRotation() const {
		return getRotation() == 90 || getRotation() == 270;
	}

	/**
	 * @brief is flipped horizontally
	 */
	inline bool isFlipX() const {
		return flipX;
	}

	/**
	 * @brief D4 Delta Composition (from this transform state).
	 * Doesn't work as expected when rotating flipping components with pins.
	 * @param target target transform state
	 * @return D4 delta composition (to go from current state to target state)
	 */
	inline Transform delta(const Transform& target) const {
	    return inverse().compose(target);
	}

	/**
	 * @brief Inverse of a transform in D4.
	 */
	inline Transform inverse() const {
		if(!flipX) {
			return Transform((360 - getRotation()) % 360, false);
		} else {
			return Transform(getRotation(), true); // flips are self-inverse, but rotation stays
		}
	}

	/**
	 * @brief compose: apply a (this) then b
	 */
	inline Transform compose(Transform b) const {
		if(!isFlipX()) {
			return Transform((getRotation() + b.getRotation()) % 360, b.isFlipX());
		} else {
			// When already flipped, rotation direction of b is mirrored
			return Transform((getRotation() + (360 - b.getRotation())) % 360, !b.isFlipX());
		}
	}

	/**
	 * Normalize angle to one of: 0, 90, 180, 270 (with rounding).
	 * @param degrees angle in degrees, any integer
	 * @return one of: 0, 90, 180, 270
	 */
	inline static int normalizeRotation(int degrees) noexcept {
		return (((degrees % 360) + 360) % 360 + 45) / 90 % 4 * 90;
	}

	inline bool operator==(const Transform& other) const {
		return isFlipX() == other.isFlipX() && getRotation() == other.getRotation();
	}

private:
	int rotation;
	bool flipX;
};
} // namespace bend