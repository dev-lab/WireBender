/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Connector.h"

namespace bend {
/**
 * @brief Wraps Connector and direction
 * Non-owning by default.
 */
class ConnectorEnd {
public:
	/**
	 * @brief ConnectorEnd
	 * @param connector Router Connector
	 * @param front true if front end, false if back end
	 */
	explicit ConnectorEnd(const Connector& connector, bool front) noexcept
		: connector(connector), front(front) {}

	/**
	 * @brief Default constructor
	 */
	ConnectorEnd() noexcept = default;

	/**
	 * @brief Get connector pointer
	 */
	Avoid::ConnRef* get() const noexcept {
		return connector.get();
	}

	/**
	 * @brief Get connector
	 */
	const Connector& getConnector() const noexcept {
		return connector;
	}

	/**
	 * @brief Check if valid
	 */
	bool isValid() const noexcept {
		return connector.isValid();
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
	bool operator==(const ConnectorEnd& other) const noexcept {
		return connector == other.connector && front == other.front;
	}

	bool operator!=(const ConnectorEnd& other) const noexcept {
		return !(*this == other);
	}

	Avoid::ConnRef* operator->() const noexcept {
		return connector.get();
	}

	/**
	 * @brief Get Id
	 */
	unsigned int getId() const noexcept {
		return isValid() ? get()->id() : 0u;
	}

	/**
	 * @brief Is front connector end?
	 */
	unsigned int isFront() const noexcept {
		return front;
	}

	/**
	 * @brief Is back connector end?
	 */
	unsigned int isBack() const noexcept {
		return !isFront();
	}

	static bool isJunction(const Avoid::ConnEnd& end) {
		return Avoid::ConnEndJunction == end.type();
	}

	static bool isShapePin(const Avoid::ConnEnd& end) {
		return Avoid::ConnEndShapePin == end.type();
	}

	/**
	 * @brief not safe for empty connector
	 * @return Connector End
	 */
	Avoid::ConnEnd getEnd(bool front) const {
		auto ends = getConnector().endpoints();
		return front ? ends.first : ends.second;
	}

	/**
	 * @return Connector End
	 */
	Avoid::ConnEnd getEnd() const {
		return getEnd(isFront());
	}

	/**
	 * @return Connector End
	 */
	Avoid::ConnEnd getOppositeEnd() const {
		return getEnd(isBack());
	}

	/**
	 * @brief Get connector end position
	 */
	static Avoid::Point getEndPoint(const Avoid::ConnEnd& connEnd) {
		if(connEnd.type() == Avoid::ConnEndJunction) {
			return connEnd.junction()->recommendedPosition();
		}
		return connEnd.position();
	}

	Avoid::Point getEndPoint(bool front) const {
		if(!isValid()) return Avoid::Point();
		return getEndPoint(getEnd(front));
	}

	Avoid::Point getEndPoint() const {
		return getEndPoint(isFront());
	}

	Avoid::Point getOppositeEndPoint() const {
		return getEndPoint(isBack());
	}

	Avoid::Edge getEdge() const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		size_t s = pts.size();
		if(0 == s) {
			return {getEndPoint(), getOppositeEndPoint()};
		} else if(1 == s) {
			return {getEndPoint(), getPoint(0, pts)};
		} else {
			return {getPoint(0, pts), getPoint(1, pts)};
		}
	}

	Avoid::Edge getEndEdge() const {
		if(isEndPointSame()) {
			return getEdge();
		} else {
			return {getEndPoint(), getEdgePoint()};
		}
	}

	Avoid::Edge getOppositeEndEdge() const {
		if(isOppositeEndPointSame()) {
			return getOppositeEdge();
		} else {
			return {getOppositeEndPoint(), getOppositeEdgePoint()};
		}
	}

	Avoid::Point getEdgePoint() const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		return 0 == pts.size() ? getEndPoint() : getPoint(0, pts);
	}

	Avoid::Point getOppositeEdgePoint() const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		return 0 == pts.size() ? getOppositeEndPoint() : getOppositePoint(0, pts);
	}

	/**
	 * @brief is end point the same as first connector point
	 */
	bool isEndPointSame() const {
		return isValid() ? bend::isSame(getEndPoint(), 0 == getConnector().size() ? getOppositeEndPoint() : getEdgePoint()) : true;
	}

	/**
	 * @brief is opposite end point the same as last connector point
	 */
	bool isOppositeEndPointSame() const {
		return isValid() ? bend::isSame(getOppositeEndPoint(), 0 == getConnector().size() ? getEndPoint() : getOppositeEdgePoint()) : true;
	}

	Avoid::Edge getOppositeEdge() const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		size_t s = pts.size();
		if(0 == s) {
			return {getOppositeEndPoint(), getEndPoint()};
		} else if(1 == s) {
			return {getOppositeEndPoint(), getOppositePoint(0, pts)};
		} else {
			return {getOppositePoint(0, pts), getOppositePoint(1, pts)};
		}
	}

	/**
	 * @brief is connector end both orthogonal (x or y) and collinear with other edge?
	 * @param other edge to check
	 */
	bool isOrthogonalCollinear(const Avoid::Edge& other) const {
		return bend::isOrthogonalCollinear(getEndEdge(), other);
	}

	/**
	 * @brief is connector end both orthogonal (x or y) and collinear with other connector end?
	 * @param other edge to check
	 */
	bool isOrthogonalCollinear(const ConnectorEnd& other) const {
		return isOrthogonalCollinear(other.getEndEdge());
	}

	/**
	 * @brief is connector ends both orthogonal (x or y) and collinear?
	 * @return true if connector endpoints can be adjusted only along the same line
	 */
	bool isOrthogonalCollinear() const {
		if(getConnector().size() > 2) return false;
		return isOrthogonalCollinear(getOppositeEndEdge());
	}

	bool isOrthogonalX() const {
		return bend::isOrthogonalX(getEdge());
	}

	bool isOrthogonalY() const {
		return bend::isOrthogonalY(getEdge());
	}

protected:
	inline size_t getPointIndex(size_t i) const {
		return getPointIndex(i, getConnector().size());
	}

	inline size_t getOppositePointIndex(size_t i) const {
		return getOppositePointIndex(i, getConnector().size());
	}

	inline size_t getPointIndex(size_t i, size_t s) const {
		assert(i < s);
		return isFront() ? i : (s - i - 1);
	}

	inline size_t getOppositePointIndex(size_t i, size_t s) const {
		assert(i < s);
		return isBack() ? i : (s - i - 1);
	}

	inline const Avoid::Point& getPoint(size_t i, const std::vector<Avoid::Point>& pts) const {
		return pts[getPointIndex(i, pts.size())];
	}

	inline const Avoid::Point& getOppositePoint(size_t i, const std::vector<Avoid::Point>& pts) const {
		return pts[getOppositePointIndex(i, pts.size())];
	}

	inline const Avoid::Point& getPoint(size_t i) const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		return pts[getPointIndex(i, pts.size())];
	}

	inline const Avoid::Point& getOppositePoint(size_t i) const {
		const std::vector<Avoid::Point>& pts = getConnector().getPoints();
		return pts[getOppositePointIndex(i, pts.size())];
	}

private:
	Connector connector;
	bool front;
};
} // namespace bend
