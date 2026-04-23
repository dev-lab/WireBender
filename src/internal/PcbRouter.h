#pragma once

#include "PcbPostProcessor.h"
#include "WireBenderTypes.h"
#include "core/NetRegistry.h"
#include "core/ShapeRegistry.h"
#include "libavoid/libavoid.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace WireBender {

/**
 * Router for PCB nets.
 * TODO: cache state
 */
class PcbRouter {
public:
	/**
	 * Routes PCB pad clusters as minimum Steiner trees using libavoid
	 * with PolyLineRouting (non-orthogonal) and no component obstacles.
	 * @param nets nets to route
	 * @return route result
	 */
	PcbRouteResult route(const std::vector<PcbNet>& nets) {
		// libavoid PolyLine router — no right-angle constraint
		Avoid::Router router(Avoid::PolyLineRouting);
		router.setTransactionUse(true);

		// Penalise crossings heavily to force detours
		router.setRoutingPenalty(Avoid::crossingPenalty, 10000.0);
		// Force wires to stay at least 5 pixels away from the pad boundaries
		router.setRoutingParameter(Avoid::shapeBufferDistance, 5.0);

		ShapeRegistry shapeRegistry(&router);
		NetRegistry netRegistry(&router);

		for(const auto& net: nets) {
			size_t pSize = net.pads.size();
			if(pSize < 2) continue;

			std::vector<Avoid::ShapeRef*> padShapes;

			// 1. Create fixed pads as physical shapes (obstacles)
			// This gives libavoid a visibility graph to route around!
			for(size_t i = 0; i < pSize; ++i) {
				const Point2D& p = net.pads[i];

				// Component: cx, cy, width, height, pins
				// Giving it an 8x8 size so wires stay well away from the center
				Component comp{{{p.x, p.y}, bend::Transform()}, 8.0, 8.0, {{1, "1", 4.0, 4.0, Avoid::ConnDirAll}}};

				Avoid::ShapeRef* shapeRef = shapeRegistry.registerComp(net.name + "." + std::to_string(i), comp);
				netRegistry.registerShapePinNet(shapeRef->id(), 1, net.name);
				padShapes.push_back(shapeRef);
			}

			// 2. Minimum Spanning Tree (Prim's Algorithm) to connect pads directly
			std::vector<bool> inTree(pSize, false);
			inTree[0] = true;

			for(size_t edges = 0; edges < pSize - 1; ++edges) {
				double minSqDist = 1e12;
				size_t bestU = 0, bestV = 0;

				for(size_t u = 0; u < pSize; ++u) {
					if(inTree[u]) {
						for(size_t v = 0; v < pSize; ++v) {
							if(!inTree[v]) {
								double dx = net.pads[u].x - net.pads[v].x;
								double dy = net.pads[u].y - net.pads[v].y;
								double sqDist = dx * dx + dy * dy;
								if(sqDist < minSqDist) {
									minSqDist = sqDist;
									bestU = u;
									bestV = v;
								}
							}
						}
					}
				}

				inTree[bestV] = true;

				// Create direct connection between the shape pins
				auto* c = new Avoid::ConnRef(&router, Avoid::ConnEnd(padShapes[bestU], 1), Avoid::ConnEnd(padShapes[bestV], 1));
				c->setRoutingType(Avoid::ConnType_PolyLine);
				netRegistry.cacheIdNet(c->id(), net.name);
			}
		}

		router.processTransaction();

		PcbRouteResult result;
		buildResult(netRegistry, {}, result);

		// --- Execute Post-Processing Pass ---
		// Uses 4.0 threshold for clearance checking, relaxes anything sharper than 110 deg
		PcbPostProcessor processor(nets, 4.0, 110.0);
		return processor.process(result);
	}

protected:
	/**
	 * Build result from routed data.
	 * @param netRegistry access to registries and router
	 * @param filterNets if not empty, include nets of interest
	 * @param result result to build
	 */
	void buildResult(const NetRegistry& netRegistry, const std::vector<std::string>& filterNets, PcbRouteResult& result) {
		std::set<std::string> filter(filterNets.begin(), filterNets.end());
		const bool filtered = !filter.empty();

		auto addJunction = [&](const bend::Junction& j, const bend::Connector& c, bool front, std::string net) {
			if(!j) return;
			netRegistry.cacheIdNet(j.getId(), net);
			Avoid::Point pos = j->recommendedPosition();
			result.junctions.push_back({net, {pos.x, pos.y}});
			WB_LOG << "Junction point added: " << bend::toStringPoint(pos) << "\n";
		};

		for(Avoid::ConnRef* conn: netRegistry.getConnections()) {
			bend::Connector connector(conn);
			std::string net = netRegistry.getNet(connector);
			if(filtered && !filter.count(net)) continue;
			const auto& r = connector->displayRoute();
			if(r.size() < 2) continue;
			std::vector<Avoid::Point> pts(r.ps.begin(), r.ps.end());
			WB_LOG << "Wire added: ";
			bool f = true;
			for(auto& p: pts) {
				if(f) {
					f = false;
				} else {
					WB_LOG << ", ";
				}
				WB_LOG << bend::toStringPoint(p);
			}
			WB_LOG << "\n";
			std::vector<Point2D> points(pts.size());
			std::transform(pts.begin(), pts.end(), points.begin(), [](const Avoid::Point& p) -> Point2D {
				return {p.x, p.y};
			});
			result.wires.push_back({net, std::move(points)});
			auto junctions = connector.junctions();
			addJunction(junctions.first, connector, true, net);
			addJunction(junctions.second, connector, false, net);
		}
	}
};
} // namespace WireBender