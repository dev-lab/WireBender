/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "NetPin.h"
#include "WireBenderTypes.h"
#include "bend/utils.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using RawNets = std::map<std::string, std::vector<NetPin>>;

/**
 * NetList.
 * Wrapper around the raw net map produced by makeNets().
 *
 * Key design decisions:
 * - All expensive derived attributes (bus detection, bus ordering, etc.)
 *	 are computed once on construction and cached.
 * - The class is immutable after construction: all public accessors are const.
 * - "Bus" detection uses a statistical outlier heuristic: a net whose size
 *	 is >= BUS_SIGMA_FACTOR standard deviations above the mean is a bus.
 *	 This mirrors how a human reads a netlist: VCC/GND look obviously big.
 * - Bus vertical order is determined by name heuristics first (positive
 *	 rails > negative rails > ground), then by alphabetical name as a
 *	 tie-breaker, so +15V sits above -15V which sits above GND.
 *
 * Usage:
 * const NetList nl(makeNets());			// move — zero extra copy
 * // or, if you need to keep the raw map separately:
 * auto rawNets = makeNets();
 * const NetList nl(rawNets);				// copy
 *
 * for (const auto& name : nl.busNames())
 *	 std::cout << name << " busLevel=" << nl.busLevel(name) << "\n";
 *
 * for (const auto& name : nl.signalNames())
 *	 std::cout << name << "\n";
 *
 */
class NetList {
public:
	/**
	 * How many standard deviations above the mean a net's pin-count must be
	 * before it is classified as a bus.  Tune this to taste.
	 * Threshold = mean + FACTOR*stddev.  With the sample netlist:
	 *	 mean=4.29, stddev=1.91
	 *	 FACTOR=0.5 → threshold≈5.24: catches VCC(6) and GND(8),
	 *								  excludes CLK/DATA(3), SIG_*(4), RST(2).
	 * Raise this value if non-power nets with many stubs are mis-classified.
	 */
	static constexpr double BUS_SIGMA_FACTOR = 0.5;

	/**
	 * Create NetList (copy from lvalue).
	 * @param nets raw nets to copy
	 * @param classificationOverride optional overriden nets classification (only those differ)
	 */
	explicit NetList(const RawNets& nets, const std::vector<WireBender::NetClassification>& classificationOverride = {})
		: nets(nets) {
		computeAll(classificationOverride);
	}

	/**
	 * Create NetList (move from an rvalue).
	 * This is the important overload: it prevents the dangling-reference bug
	 * that occurs when a temporary RawNets is passed directly.
	 * @param nets ras nets to move
	 * @param classificationOverride optional overriden nets classification (only those differ)
	 */
	explicit NetList(RawNets&& nets, const std::vector<WireBender::NetClassification>& classificationOverride = {})
		: nets(std::move(nets)) {
		computeAll(classificationOverride);
	}

	// ── Raw access ──────────────────────────────────────────────────────────

	/**
	 * Get raw nets data (net names per pins).
	 * @return raw nets
	 */
	const RawNets& getRawNets() const {
		return nets;
	}

	/**
	 * Get net names (buses + signals, unordered).
	 * @return net names
	 */
	std::vector<std::string> netNames() const {
		std::vector<std::string> names;
		names.reserve(nets.size());
		for(const auto& [name, _]: nets) names.push_back(name);
		return names;
	}

	/**
	 * Get the raw pins for a net. Throws if name is unknown.
	 * @param name existing net name
	 * @return vector of NetPin
	 */
	const std::vector<NetPin>& pins(const std::string& name) const {
		return nets.at(name);
	}

	/**
	 * Get amount of component pins connected to the net.
	 * @param name net name
	 * @return amount of connected component pins in a net
	 */
	size_t size(const std::string& name) const {
		return nets.at(name).size();
	}

	/**
	 * Check if this net is a statistical outlier in pin-count (bus heuristic).
	 * @param name net name
	 * @return true if the net is probably a bus
	 */
	bool isBus(const std::string& name) const {
		return buses.count(name) > 0;
	}

	/**
	 * Check if this net is NOT a bus (ordinary signal wire).
	 * @param name net name
	 * @return true if the net is probably a signal line
	 */
	bool isSignal(const std::string& name) const {
		return !isBus(name);
	}

	/**
	 * Check if this bus is a positive power rail (VCC, VDD, +nV, etc.).
	 * Tier 0 in the bus ordering.	 Draws above the component area.
	 * @param name net name
	 * @return true if the net is probably a positive power rail
	 */
	bool isPositiveRail(const std::string& name) const {
		return isBus(name) && busTier(name) == 0;
	}

	/**
	 * Check if this bus is a negative power rail (VSS, -nV, etc.).
	 * Tier 2 in the bus ordering.	 Draws below the component area.
	 * @param name net name
	 * @return true if the net is probably a negative power rail
	 */
	bool isNegativeRail(const std::string& name) const {
		return isBus(name) && busTier(name) == 2;
	}

	/**
	 * Get bus polarity tier used for vertical ordering:
	 *	  0 = positive rail (VCC, VDD, +nV, bare digits)
	 *	  1 = unclassified power
	 *	  2 = negative rail (VSS, -nV)
	 *	  3 = ground (GND)
	 * @param name net name
	 * @return bus polarity tier
	 */
	int busTier(const std::string& name) const {
		if(isGnd(name)) return 3;
		const std::string lo = bend::toLower(name);
		if(name[0] == '-' || lo.find("vss") != std::string::npos || lo.find("-v") != std::string::npos)
			return 2;
		if(name[0] == '+' || lo.find("vcc") != std::string::npos
		   || lo.find("vdd") != std::string::npos
		   || lo.find("pwr") != std::string::npos
		   || lo.find("v+") != std::string::npos)
			return 0;
		if(!lo.empty() && std::isdigit(static_cast<unsigned char>(lo[0])))
			return 0;
		return 1;
	}

	/**
	 * Check if this bus is the ground reference.
	 * Detection: name contains "GND" or "AGND" (case-insensitive),
	 * and the net is a bus.  Falls back to the bus with the most pins if no
	 * name match is found.
	 * @param name net name
	 * @return true if the bus is GND
	 */
	bool isGnd(const std::string& name) const {
		return isBus(name) && (name == gndName);
	}

	/**
	 * Get vertical drawing level for a bus (0 = topmost, higher = lower).
	 * Only meaningful when isBus() is true.  Returns -1 for signal nets.
	 * @param name net name
	 * @return vertical bus level
	 */
	int busLevel(const std::string& name) const {
		auto it = busLevels.find(name);
		if(it == busLevels.end()) return -1;
		return it->second;
	}

	/**
	 * Get buses sorted by their drawing level (top-to-bottom).
	 * Typical result: ["+15V", "-15V", "GND"] or ["VCC", "GND"].
	 * @return sorted bus names
	 */
	const std::vector<std::string>& busNames() const {
		return busNamesSorted;
	}

	/**
	 * Get signal nets (non-bus), in alphabetical order.
	 * @return signal net names
	 */
	const std::vector<std::string>& signalNames() const {
		return signals;
	}

	/**
	 * Get statistics: mean pin count.
	 * @return mean pin count
	 */
	double meanPinCount() const {
		return mean;
	}

	/**
	 * Get statistics: standard deviation pin count.
	 * @return standard deviation pin count
	 */
	double stddevPinCount() const {
		return stddev;
	}

private:
	/**
	 * Computation (runs once in constructor).
	 */
	void computeAll(const std::vector<WireBender::NetClassification>& classificationOverride) {
		if(nets.empty()) return;

		computeStatistics();
		classifyNets();
		computeBusOrder();
		processOverrides(classificationOverride);
	}

	/**
	 * Compute mean and stddev of pin counts across all nets.
	 */
	void computeStatistics() {
		std::vector<double> counts;
		counts.reserve(nets.size());
		for(const auto& [_, pins]: nets)
			counts.push_back(static_cast<double>(pins.size()));

		mean = std::accumulate(counts.begin(), counts.end(), 0.0)
			   / static_cast<double>(counts.size());

		double variance = 0.0;
		for(double c: counts) variance += (c - mean) * (c - mean);
		variance /= static_cast<double>(counts.size());
		stddev = std::sqrt(variance);
	}

	/**
	 * Classify each net as bus or signal.
	 */
	void classifyNets() {
		const double threshold = std::max(mean + BUS_SIGMA_FACTOR * stddev, nets.size() > 2 ? 3.0 : 10.0);

		for(const auto& [name, pins]: nets) {
			if(static_cast<double>(pins.size()) >= threshold)
				buses.insert(name);
			else
				signals.push_back(name);
		}
		std::sort(signals.begin(), signals.end());

		// Identify GND: prefer a name containing "gnd" (case-insensitive),
		// fall back to the bus with the most pins.
		std::string fallbackGnd;
		size_t maxPins = 0;
		for(const auto& name: buses) {
			std::string lower = bend::toLower(name);
			if(lower == "gnd" || lower == "0v" || lower.find("gnd") != std::string::npos) { // GND, 0V, AGND
				gndName = name;																// explicit name match wins immediately
				break;
			}
			if(nets.at(name).size() > maxPins) {
				maxPins = nets.at(name).size();
				fallbackGnd = name;
			}
		}
		if(gndName.empty()) gndName = fallbackGnd;
	}

	/**
	 * Assign a vertical drawing level to each bus.
	 * Ordering heuristic (lower number = drawn higher / more positive):
	 *	 1. Positive rails:	 name starts with '+', or contains "VCC"/"VDD"/"V+" / a digit
	 *						 followed by 'V' with no leading '-'.
	 *	 2. Unlabeled / other power nets that are not GND.
	 *	 3. Negative rails:	 name starts with '-', or contains "VSS" / "-V".
	 *	 4. Ground:			 isGnd().
	 *
	 * Within each tier, sort by decreasing numeric value (so +15V before +5V),
	 * then alphabetically.
	 */
	void computeBusOrder() {
		// Assign each bus to a tier.
		struct BusSortKey {
			int tier;		   // 0=positive, 1=other, 2=negative, 3=gnd
			double numericVal; // extracted voltage (negative = sorts later)
			std::string name;
		};

		std::vector<BusSortKey> keys;
		keys.reserve(buses.size());
		for(const auto& name: buses) {
			int tier = busTier(name);
			double val = extractVoltage(name);
			keys.push_back({tier, val, name});
		}

		std::sort(keys.begin(), keys.end(), [](const BusSortKey& a, const BusSortKey& b) {
			if(a.tier != b.tier) return a.tier < b.tier;
			// Within positive tier: higher voltage on top (larger val first)
			// Within negative tier: less negative on top (larger val first)
			if(std::abs(a.numericVal - b.numericVal) > 0.001)
				return a.numericVal > b.numericVal;
			return a.name < b.name;
		});

		for(int level = 0; level < static_cast<int>(keys.size()); ++level) {
			busLevels[keys[level].name] = level;
			busNamesSorted.push_back(keys[level].name);
		}
	}

	/**
	 * Patch calculated rails with user preferences
	 * @param classificationOverride user overrides to default classification
	 */
	void processOverrides(const std::vector<WireBender::NetClassification>& classificationOverride) {
		for(const WireBender::NetClassification& c: classificationOverride) {
			bool wasBus = buses.find(c.name) != buses.end();
			if(c.isBus != wasBus) {
				// clean signal/bus
				if(c.isBus) {
					auto it = std::find(signals.begin(), signals.end(), c.name);
					if(it != signals.end()) {
						signals.erase(it);
					}
					buses.insert(c.name);
					busNamesSorted.push_back(c.name);
				} else {
					buses.erase(c.name);
					busLevels.erase(c.name);
					auto it = std::find(busNamesSorted.begin(), busNamesSorted.end(), c.name);
					if(it != busNamesSorted.end()) {
						busNamesSorted.erase(it);
					}
					signals.push_back(c.name);
				}
			}
			if(c.isBus) {
				busLevels[c.name] = c.busLevel;
			}
		}
		std::sort(signals.begin(), signals.end());
		std::sort(busNamesSorted.begin(), busNamesSorted.end(), [&](const std::string& a, const std::string& b) {
			return busLevels[a] < busLevels[b];
		});
	}

	// ── Helpers ─────────────────────────────────────────────────────────────

	/**
	 * Extract a numeric voltage from a net name for sort purposes.
	 * "+15V" → 15.0, "-15V" → -15.0, "VCC" → 0.0 (fallback).
	 * @param name net name
	 * @return numeric voltage
	 */
	static double extractVoltage(const std::string& name) {
		// Strip leading '+'/'-', then parse digits and optional decimal.
		bool negative = (!name.empty() && name[0] == '-');
		std::string digits;
		for(char c: name) {
			if(std::isdigit(static_cast<unsigned char>(c)) || c == '.') digits += c;
		}
		if(digits.empty()) return 0.0;
		double val = std::stod(digits);
		return negative ? -val : val;
	}

	RawNets nets; // owned — never a dangling reference

	double mean = 0.0;	 // statistics: mean pin count
	double stddev = 0.0; // statistics: standard deviation pin count

	std::set<std::string> buses;			 // bus net names
	std::map<std::string, int> busLevels;	 // bus → drawing row
	std::vector<std::string> busNamesSorted; // buses top→bottom
	std::vector<std::string> signals;		 // signal nets, sorted
	std::string gndName;					 // which bus is GND
};
