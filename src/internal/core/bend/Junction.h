#pragma once

#include "libavoid/libavoid.h"
#include "orthogonal.h"

namespace bend {
/**
 * @brief Thin wrapper around Avoid::JunctionRef*
 * Non-owning by default.
 */
class Junction {
public:
	/**
	 * @brief Construct from raw pointer
	 * @param junction pointer to Avoid::JunctionRef (non-owning)
	 */
	explicit Junction(Avoid::JunctionRef* junction) noexcept
		: junction(junction) {}

	/**
	 * @brief Construct from raw pointer
	 * @param end connector end
	 */
	explicit Junction(const Avoid::ConnEnd& end) noexcept
		: Junction(end.type() == Avoid::ConnEndJunction ? end.junction() : nullptr) {
	}

	/**
	 * @brief Default constructor (null junction)
	 */
	Junction() noexcept = default;

	/**
	 * @brief Get underlying pointer
	 */
	Avoid::JunctionRef* get() const noexcept {
		return junction;
	}

	/**
	 * @brief Check if valid
	 */
	bool isValid() const noexcept {
		return junction != nullptr;
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
	bool operator==(const Junction& other) const noexcept {
		return junction == other.junction;
	}

	bool operator!=(const Junction& other) const noexcept {
		return !(*this == other);
	}

	Avoid::JunctionRef* operator->() const noexcept {
		return junction;
	}

	/**
	 * @brief Get Id
	 */
	unsigned int getId() const noexcept {
		return isValid() ? get()->id() : 0u;
	}

	bool isSamePosition() const {
		if(!isValid()) return false;
		return bend::isSame(get()->position(), get()->recommendedPosition());
	}

	bool isSame(const Avoid::Point& point) const {
		if(!isValid()) return false;
		return bend::isSame(get()->recommendedPosition(), point);
	}

private:
	Avoid::JunctionRef* junction = nullptr;
};
} // namespace bend