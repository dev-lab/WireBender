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
			bend::ConnectorEnd from(conn, true);
			bend::ConnectorEnd to(conn, false);
			if(pts.empty()) {
				pts.push_back(from.getEndPoint());
				pts.push_back(to.getEndPoint());
			} else {
				if(!from.isEndPointSame()) {
					pts.insert(pts.begin(), from.getEndPoint());
				}
				if(!to.isEndPointSame() || pts.size() < 2) {
					pts.push_back(to.getEndPoint());
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
