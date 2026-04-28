/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "RouterState.h"
#include <map>
#include <set>
#include <string>

/**
 * Wraps the raw router state output from SchematicRouter and derives all geometric
 * information of wires and junction points from it.
 */
class RoutingResult {
public:
	/**
	 * Creates full routing result (all nets).
	 * @param routerState Schematic Router state
	 */
	explicit RoutingResult(const RouterState& routerState) {
		build(routerState, {});
	}

	/**
	 * Creates filtered routing result: only connectors whose net is in filterNets.
	 * Used by moveComponent to avoid processing the full c2net (40+ connectors)
	 * when only 2-3 nets were affected.
	 * @param routerState Schematic Router state
	 * @param filterNets nets to take into account
	 */
	RoutingResult(const RouterState& routerState,
				  const std::set<std::string>& filterNets) {
		build(routerState, filterNets);
	}

	/**
	 * Get all wire segments.
	 * @return all wires
	 */
	const std::map<std::string, std::vector<std::vector<Avoid::Point>>>& getWires() const {
		return wires;
	}

	/**
	 * Get all junction dot locations.
	 * @return junction dots
	 */
	const std::map<std::string, std::vector<Avoid::Point>>& getDots() const {
		return dots;
	}

private:
	/**
	 * Build routing result.
	 * @param routerState Schematic Router state
	 * @param filter nets to take into account
	 */
	void build(const RouterState& routerState, const std::set<std::string>& filter) {
		const NetRegistry& netRegistry = routerState.getNetRegistry();
		const ShapeRegistry& shapeRegistry = routerState.getShapeRegistry();
#ifdef WB_DEBUG
		std::string traceFileName = makeTimestampFilename();
		netRegistry.getRouter()->outputDiagram(traceFileName);
		netRegistry.getRouter()->outputDiagramSVG(traceFileName);
#endif
		wires.clear();
		dots.clear();
		for(const auto& [id, conn]: routerState.getConnectors()) {
			std::string net = netRegistry.getNet(conn);
			if(!filter.empty() && !filter.count(net)) continue;
			const auto& r = conn.getPoints();
			std::vector<Avoid::Point> pts(r.begin(), r.end());
			std::pair<Avoid::ConnEnd, Avoid::ConnEnd> ends = conn.endpoints();
			auto getEndPoint = [&](unsigned int id, const bend::Connector& conn, bool from) {
				bend::ConnectorEnd connEnd(conn, from);
				Avoid::Point point = connEnd.getEndPoint();
				const Avoid::ConnEnd& end = from ? ends.first : ends.second;
				Avoid::Point pinPoint = shapeRegistry.pinWorld(end);
				if(!bend::isSame(point, pinPoint)) {
					std::string name = shapeRegistry.getShapeName(end.shape() ? end.shape()->id() : 0u);
					WB_LOG << "Warning: " << net << " connection[" << id << "] " << (from ? "start" : "end")
						<< " " << bend::toStringPoint(point) << " differs from "
						<< name << "." << std::to_string(end.pinClassId()) << " pin " << bend::toStringPoint(pinPoint) << "\n";
					point = pinPoint;
				}
				return point;
			};
			Avoid::Point fromPoint = getEndPoint(id, conn, true);
			Avoid::Point toPoint = getEndPoint(id, conn, false);
			if(pts.empty()) {
				pts.push_back(fromPoint);
				pts.push_back(toPoint);
			} else {
				if(!bend::isSame(fromPoint, pts.front())) {
					bend::ConnectorEnd fromEnd(conn, true);
					if(bend::isSame(fromEnd.getEndPoint(), pts.front())) {
						// libavoid connection first point goes to wrong shape pin coordinates
						pts.front() = fromPoint;
					} else {
						pts.insert(pts.begin(), fromPoint);
					}
				}
				if(!bend::isSame(toPoint, pts.back()) || pts.size() < 2) {
					bend::ConnectorEnd toEnd(conn, false);
					if(bend::isSame(toEnd.getEndPoint(), pts.back())) {
						// libavoid connection last point goes to wrong shape pin coordinates
						pts.back() = toPoint;
					} else {
						pts.push_back(toPoint);
					}
				}
			}
			wires[net].push_back(std::move(pts));
		}
		for(const auto& [id, je]: routerState.getJunctionEdges()) {
			std::string net = netRegistry.getNet(je.getJunction());
			if(!filter.empty() && !filter.count(net)) continue;
			if(je.size() >= 3) {
				dots[net].push_back(je.getJunction()->recommendedPosition());
			}
		}
	}

	std::map<std::string, std::vector<std::vector<Avoid::Point>>> wires;
	std::map<std::string, std::vector<Avoid::Point>> dots;
};
