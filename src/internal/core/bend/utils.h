/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "libavoid/libavoid.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>

namespace bend {

const double EPS = 0.001;
const double NEPS = -EPS;

/**
 * @brief Hash functor for std::pair<int, int>, suitable for use as an
 *        unordered_map or unordered_set key.
 *
 * Uses a Boost-style hash-combine formula to mix the two halves.
 */
struct PairHash {
	/**
	 * @param p The pair to hash.
	 * @return Combined hash value.
	 */
	std::size_t operator()(const std::pair<int, int>& p) const {
		auto h1 = std::hash<int>{}(p.first);
		auto h2 = std::hash<int>{}(p.second);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

inline std::string toLower(const std::string& s) {
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(),
				   [](unsigned char c) {
					   return static_cast<char>(std::tolower(c));
				   });
	return r;
}

/**
 * Point to string, without wrapping to [].
 * @tparam T point class, with x and y
 * @param p point
 */
template<class T>
std::string toStringP(const T& p) {
	return std::to_string((int)std::round(p.x))
		   + "," + std::to_string((int)std::round(p.y));
}

/**
 * Point to string.
 * @tparam T point class, with x and y
 * @param p point
 */
template<class T>
std::string toStringPoint(const T& p) {
	return "[" + toStringP(p) + "]";
}

/**
 * Point to string.
 * @tparam T point class, with x and y
 * @param p point
 */
template<class T>
std::string toStringPoint(const T& x, const T& y) {
	return "[" + std::to_string((int)std::round(x))
		   + "," + std::to_string((int)std::round(y)) + "]";
}

/**
 * Edge to string.
 * @tparam T edge class, with a and b being point classes
 * @param e edge
 */
template<class T>
std::string toStringEdge(const T& e) {
	return toStringPoint(e.a) + "-" + toStringPoint(e.b);
}

/**
 * @brief Box to string.
 * @param box box
 */
inline std::string toStringBox(const Avoid::Box& box) {
	return "{" + toStringPoint(box.min) + "-" + toStringPoint(box.max)
		   + " " + std::to_string((int)std::round(box.width()))
		   + " X " + std::to_string((int)std::round(box.height())) + "}";
}

/**
 * @brief Convert Avoid::ConnDirFlags to human-readable string.
 *
 * Examples:
 *  - ConnDirUp -> "Up"
 *  - ConnDirUp | ConnDirLeft -> "Up|Left"
 *  - ConnDirAll -> "All"
 */
inline std::string toStringDirection(Avoid::ConnDirFlags flags) {
	if(flags == Avoid::ConnDirAll) {
		return "All";
	}
	if(flags == 0) {
		return "None";
	}

	std::vector<std::string> parts;

	if(flags & Avoid::ConnDirUp) {
		parts.emplace_back("Up");
	}
	if(flags & Avoid::ConnDirDown) {
		parts.emplace_back("Down");
	}
	if(flags & Avoid::ConnDirLeft) {
		parts.emplace_back("Left");
	}
	if(flags & Avoid::ConnDirRight) {
		parts.emplace_back("Right");
	}

	// Join with '|'
	std::string result;
	for(size_t i = 0; i < parts.size(); ++i) {
		if(i > 0) result += "|";
		result += parts[i];
	}

	return result;
}

/**
 * Round float or double to int.
 * @tparam T float or double
 * @param v value to be rounded to int
 */
template<class T>
inline int iround(T v) {
	return static_cast<int>(std::round(v));
}

/**
 * @brief calculate squared distance
 */
inline double distance2(double dx, double dy) {
	return dx * dx + dy * dy;
}

/**
 * @brief calculate squared distance between points
 * @tparam T point class, with x and y
 * @param a point A
 * @param b point B
 */
template<class T>
double distance2(const T& a, const T& b) {
	return distance2(a.x - b.x, a.y - b.y);
}

inline bool is0(double value) {
	return value < EPS && value > NEPS;
}

inline bool equal(double a, double b) {
	return std::abs(a - b) < EPS;
}

} // namespace bend
