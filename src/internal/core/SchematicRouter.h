#pragma once

#include "NetList.h"
#include "NetRegistry.h"
#include "RouterState.h"
#include "RouterStats.h"
#include "SchematicRouterOptions.h"
#include "ShapeRegistry.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * Schematic Router.
 * Orchestrates the full libavoid routing pipeline for a schematic.
 *
 * Delegates to:
 *	 ShapeRegistry — component shapes and pin ID mapping
 *	 NetLabeller   — post-routing net-name flood-fill
 *
 * Responsibilities kept here:
 *	 - Router construction and penalty/parameter/option configuration
 *	 - Bus Y-position computation (uses NetList::isPositiveRail())
 *	 - Bus backbone routing
 *	 - Signal net routing (two-pin direct + hyperedge star warm-start)
 *	 - Two processTransaction passes (initial route + MTST optimise)
 *	 - Hyperedge-improver result ingestion
 *
 * Usage:
 *	 SchematicRouterOptions opts;
 *	 opts.crossingPenalty = 3000;
 *
 *	 SchematicRouter sr(opts);
 *	 sr.build(COMPS, nl);
 *	 sr.printStats();
 *	 renderer.render("out.svg", COMPS, nl, sr.connectorNetMap());
 */
class SchematicRouter {
public:
	/**
	 * Create Schematic Router.
	 * @param opts configuration options
	 */
	explicit SchematicRouter(const SchematicRouterOptions& opts = {})
		: opts(opts) {}

	/**
	 * Clean the libavoid resources.
	 */
	~SchematicRouter() {
		delete router;
	}

	SchematicRouter(const SchematicRouter&) = delete;
	SchematicRouter& operator=(const SchematicRouter&) = delete;

	/**
	 * Build and route the complete schematic. Call exactly once.
	 * @param comps components
	 * @param nl netlist
	 */
	void build(const Components& comps, const NetList& nl) {
		if(router) throw std::logic_error("SchematicRouter::build() called twice");
		components = &comps;
		netlist = &nl;

		initRouter();

		shapeRegistry = ShapeRegistry(router, opts.pinInsideOffset);
		shapeRegistry.registerAll(comps);

		netRegistry = NetRegistry(router);
		netRegistry.registerAll(shapeRegistry, netlist->getRawNets());

		const double maxLayoutY = computeMaxLayoutY();
		routeBuses(computeBusYPositions(maxLayoutY));
		routeSignals();

		processTransaction("--- processTransaction() MTST ---");
		printRoutingTrace();

		for(size_t i = 0; i < opts.maxImproveJunctionAttempts; ++i) {
			if(!improveJunctions()) break;
			processTransaction("--- processTransaction() Junctions improved (pass " + std::to_string(i) + ") ---");
			printRoutingTrace();
		}
		improveJunctions(true);
	}

	/**
	 * Get connector ref pointer to net name mapping.
	 * @return connector to net name mapping
	 */
	RouterState getState() const {
		return RouterState(netRegistry);
	}

	/**
	 * Incrementally re-route after moving one component to a new position.
	 * Updates only the connectors attached to that component; all other routes
	 * are unchanged. Must be called after build().
	 * @param compName name of the component to move
	 * @param placement new component placement
	 * @param newY new world-coordinate Y center of the component
	 */
	void moveComp(const std::string& compName, const bend::Placement& placement) {
		if(!router)
			throw std::logic_error("SchematicRouter::moveComp() called before build()");
		if(!components->count(compName))
			throw std::invalid_argument("SchematicRouter::moveComp(): unknown component: " + compName);

		const Component& c = components->at(compName);
		Avoid::ShapeRef* shapeRef = shapeRegistry.shape(compName);
		bend::Placement delta = c.placement.delta(placement);

		bool placementChanged = false;
		if(delta.isPosition()) {
			router->moveShape(shapeRef, delta.position.x, delta.position.y);
			placementChanged = true;
		}

#ifdef WB_DEBUG
		Avoid::Point oldCenter = shapeRef->position();
		Avoid::Box oldBox = shapeRef->routingBox();
#endif
		if(shapeRegistry.transform(shapeRef, c.placement.transform, placement.transform)) {
			placementChanged = true;
		}

		if(placementChanged) {
			try {
				router->processTransaction();
				const_cast<Components*>(components)->at(compName).placement = placement;
#ifdef WB_DEBUG
				WB_LOG << "[SchematicRouter] moveComp(" << compName << ") transformed from {center: "
					   << bend::toStringPoint(oldCenter) << ", box: " << bend::toStringBox(oldBox) << "} to {"
					   << bend::toStringPoint(shapeRef->position())
					   << ", box: " << bend::toStringBox(shapeRef->routingBox()) << "}\n";
#endif
			} catch(const std::exception& e) {
				std::cerr << "[SchematicRouter] moveComp(" << compName << ") processTransaction threw: "
						  << e.what() << "\n";
			} catch(...) {
				std::cerr << "[SchematicRouter] moveComp(" << compName << ") processTransaction threw unknown exception\n";
			}
		} else {
			WB_LOG << "[SchematicRouter] moveComp(" << compName << ") ignored, placement is the same\n";
		}
	}

	/**
	 * Print complete routing trace: all junctions, all connectors with full
	 * polyline segments, so the routing geometry can be analysed precisely.
	 */
	void printRoutingTrace() const {
		std::set<const Avoid::JunctionRef*> junctions;
		auto printJunction = [&](const Avoid::JunctionRef* j) {
			const auto pos = j->position();
			const auto rpos = j->recommendedPosition();
			WB_LOG << "	 J[" << j->id() << "]" << (netRegistry.isValid(j) ? "" : "X") << " net=" << netRegistry.getNet(j)
				   << " ptr=" << (void*)j
				   << " rpos=" << bend::toStringPoint(rpos)
				   << " pos=" << bend::toStringPoint(pos)
				   << ") fixed=" << j->positionFixed() << "\n";
		};

		auto ptrConnEnd = [&](const Avoid::ConnEnd& connEnd) -> std::string {
			Avoid::ConnEndType type = connEnd.type();
			std::string point = bend::toStringPoint(connEnd.position());
			std::string dir = bend::toStringDirection(connEnd.directions());
			point += " (" + dir + ")";
			if(type == Avoid::ConnEndPoint) {
				return "Point" + point;
			} else if(type == Avoid::ConnEndShapePin) {
				const Avoid::ShapeRef* s = connEnd.shape();
				std::string shapeName = shapeRegistry.getShapeName(s ? s->id() : 0u);
				return "Pin" + point + " P["
					   + (s ? std::to_string(s->id()) : "null") + "." + std::to_string(connEnd.pinClassId()) + "] " + shapeName
					   + (s ? bend::toStringPoint(s->position()) + " net:" + netRegistry.getNet(s->id(), connEnd.pinClassId()) : "");
			} else if(type == Avoid::ConnEndJunction) {
				const Avoid::JunctionRef* j = connEnd.junction();
				junctions.insert(j);
				return "Junc" + point + " J[" + (j ? std::to_string(j->id()) : "null") + "]"
					   + (j ? bend::toStringPoint(j->recommendedPosition()) : "");
			} else {
				return "Empty" + point;
			}
		};

		auto printConn = [&](Avoid::ConnRef* cr) {
			bend::Connector c(cr);
			const auto& r = c->displayRoute();
			if(r.size() < 2) return;
			auto ends = c.endpoints();

			WB_LOG << "	 C[" << c->id() << "]" << (netRegistry.isValid(c) ? "" : "X")
				   << (c->needsRepaint() ? "~" : "")
				   << " net=" << netRegistry.getNet(c)
				   << " from=" << ptrConnEnd(ends.first)
				   << " to=" << ptrConnEnd(ends.second)
				   << " segs=";
			for(size_t i = 0; i < r.size(); ++i) {
				if(i) WB_LOG << "->";
				WB_LOG << "(" << (int)std::round(r.ps[i].x)
					   << "," << (int)std::round(r.ps[i].y) << ")";
			}
			WB_LOG << "\n";
		};

		// --- Connectors: endpoints + complete polyline ---
		WB_LOG << "--- Connector trace ---\n";
		for(Avoid::ConnRef* c: router->connRefs) {
			printConn(c);
		}

		// --- Junctions ---
		WB_LOG << "\n--- Junction trace ---\n";
		for(const Avoid::JunctionRef* j: junctions) {
			printJunction(j);
		}
	}

	/**
	 * Calculate some useful statistics of routing.
	 * Booleans returned by Avoid::Router.exists*() methods are not enough.
	 * @return statistics related to orthogonal routing
	 */
	RouterStats getStats() const {
		RouterStats stats;
		Avoid::ConnRefList::iterator fin = router->connRefs.end();
		for(Avoid::ConnRefList::iterator i = router->connRefs.begin(); i != fin; ++i) {
			Avoid::Polygon iRoute = (*i)->displayRoute();
			if((*i)->routingType() == Avoid::ConnType_Orthogonal) {
				for(size_t iInd = 1; iInd < iRoute.size(); ++iInd) {
					if((iRoute.at(iInd - 1).x != iRoute.at(iInd).x) && (iRoute.at(iInd - 1).y != iRoute.at(iInd).y)) {
						++stats.invalidOrthogonalSegmentCount;
					}
				}
			}
			Avoid::ConnRefList::iterator j = i;
			for(++j; j != fin; ++j) {
				// Determine if this pair overlap
				Avoid::Polygon jRoute = (*j)->displayRoute();
				Avoid::ConnectorCrossings cross(iRoute, true, jRoute, *i, *j);
				cross.checkForBranchingSegments = true;
				for(size_t jInd = 1; jInd < jRoute.size(); ++jInd) {
					const bool finalSegment = ((jInd + 1) == jRoute.size());
					cross.countForSegment(jInd, finalSegment);
					stats.crossingCount += cross.crossingCount;
					if(cross.crossingFlags & Avoid::CROSSING_SHARES_PATH) {
						++stats.orthogonalSegmentOverlapCount;
						bool atEnd = cross.crossingFlags & Avoid::CROSSING_SHARES_PATH_AT_END;
						if(atEnd) ++stats.orthogonalSegmentOverlapAtEndCount;
						if(cross.crossingFlags & Avoid::CROSSING_SHARES_FIXED_SEGMENT) {
							++stats.orthogonalFixedSegmentOverlapCount;
							if(atEnd) ++stats.orthogonalFixedSegmentOverlapAtEndCount;
						}
					}
					if(cross.crossingFlags & Avoid::CROSSING_TOUCHES) {
						++stats.orthogonalTouchingSegmentCount;
					}
				}
			}
		}
		return stats;
	}
	/**
	 * Print routing statistics.
	 * @param toOut if true print even if not debugging
	 * @return booll is stat OK
	 */
	bool printStats(bool toOut = false) const {
#ifdef WB_DEBUG
		toOut = true;
#endif
		if(toOut) {
			RouterStats stats = getStats();
			bool ok = stats.isOk();
			std::cout << "[WireBender] Hyperwires bent" << (ok ? " OK" : "... but not shiny") << ":\n"
					  << "	Orthogonal segment overlaps:       "
					  << stats.orthogonalSegmentOverlapCount << ", at ends: " << stats.orthogonalSegmentOverlapAtEndCount << "\n"
					  << "	Orthogonal fixed segment overlaps: "
					  << stats.orthogonalFixedSegmentOverlapCount << ", at ends: " << stats.orthogonalFixedSegmentOverlapAtEndCount << "\n"
					  << "	Orthogonal touching segments:      " << stats.orthogonalTouchingSegmentCount << "\n"
					  << "	Invalid orthogonal segments:       " << stats.invalidOrthogonalSegmentCount << "\n"
					  << "	Total crossing count:              " << stats.crossingCount << "\n";
			return ok;
		} else {
			return true;
		}
	}

	/**
	 * Get the schematic router configuration options in use.
	 */
	const SchematicRouterOptions& options() const {
		return opts;
	}

private:
	/**
	 * Router initialisation.
	 */
	void initRouter() {
		router = new Avoid::Router(/*Avoid::PolyLineRouting | */ Avoid::OrthogonalRouting);
		router->setTransactionUse(true);
		router->setRoutingPenalty(Avoid::segmentPenalty,
								  opts.segmentPenalty);
		router->setRoutingPenalty(Avoid::anglePenalty,
								  opts.anglePenalty);
		router->setRoutingPenalty(Avoid::crossingPenalty,
								  opts.crossingPenalty);
		router->setRoutingPenalty(Avoid::clusterCrossingPenalty,
								  opts.clusterCrossingPenalty);
		router->setRoutingPenalty(Avoid::fixedSharedPathPenalty,
								  opts.fixedSharedPathPenalty);
		router->setRoutingPenalty(Avoid::portDirectionPenalty,
								  opts.portDirectionPenalty);
		router->setRoutingParameter(Avoid::idealNudgingDistance,
									opts.idealNudgingDistance);
		router->setRoutingParameter(Avoid::shapeBufferDistance,
									opts.shapeBufferDistance);
		router->setRoutingOption(
				Avoid::nudgeOrthogonalSegmentsConnectedToShapes,
				opts.nudgeOrthogonalSegmentsConnectedToShapes);
		// improveHyperedgeRoutesMovingJunctions true by default is ok
		router->setRoutingOption(
				Avoid::penaliseOrthogonalSharedPathsAtConnEnds,
				opts.penaliseOrthogonalSharedPathsAtConnEnds);
		router->setRoutingOption(
				Avoid::nudgeOrthogonalTouchingColinearSegments,
				opts.nudgeOrthogonalTouchingColinearSegments);
		// router_->setRoutingOption(Avoid::performUnifyingNudgingPreprocessingStep, false);
		router->setRoutingOption(
				Avoid::improveHyperedgeRoutesMovingAddingAndDeletingJunctions,
				opts.improveHyperedgeRoutesMovingAddingAndDeletingJunctions);
		// router_->setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, false);
	}

	/**
	 * Computers max layout Y dimension (for bus layout).
	 * @return max Y fitting all the components
	 */
	double computeMaxLayoutY() const {
		double maxY = 0.0;
		for(const auto& [name, comp]: *components) {
			const double tempY = comp.placement.position.y + comp.getHeight() / 2.0;
			if(tempY > maxY) maxY = tempY;
		}
		return maxY;
	}

	/**
	 * Compute Y position for buses.
	 * Uses NetList::isPositiveRail().
	 * @param maxLayoutY max layout Y (for all the components)
	 * @return map of bus level per Y coordinate
	 */
	std::map<int, double> computeBusYPositions(double maxLayoutY) const {
		std::map<int, double> busY;
		int aboveIdx = 0, belowIdx = 0;
		for(const auto& name: netlist->busNames()) {
			const int lvl = netlist->busLevel(name);
			if(netlist->isPositiveRail(name)) {
				busY[lvl] = opts.busTopBase + aboveIdx++ * opts.busTopSpacing;
			} else {
				busY[lvl] = maxLayoutY + opts.busBotMargin
							+ belowIdx++ * opts.busBotSpacing;
			}
		}
		return busY;
	}

	/**
	 * Route buses.
	 * @param busY bus Y positions
	 */
	void routeBuses(const std::map<int, double>& busY) {
		for(const auto& name: netlist->busNames()) {
			WB_LOG << "--- Building " << name << " bus backbone ---\n";
			buildBusBackbone(name, busY.at(netlist->busLevel(name)));
		}
	}

	/**
	 * Route single bus.
	 * @param netName net name
	 * @param busY Y coordinate for bus
	 * @return bus junctions
	 */
	std::vector<Avoid::JunctionRef*> buildBusBackbone(const std::string& netName,
													  double busY) {
		struct PinInfo {
			std::string comp;
			unsigned int pin;
			Avoid::Point world;
		};
		std::vector<PinInfo> pins;
		for(const auto& n: netlist->pins(netName))
			pins.push_back({n.comp, n.pin, shapeRegistry.pinWorld(n.comp, n.pin)});
		std::sort(pins.begin(), pins.end(),
				  [](const PinInfo& a, const PinInfo& b) {
					  return a.world.x < b.world.x;
				  });

		std::vector<Avoid::Point> busPoints;
		for(const auto& pi: pins) {
			bool found = false;
			for(const auto& bp: busPoints)
				if(std::abs(bp.x - pi.world.x) < opts.busCollapseThreshold) {
					found = true;
					break;
				}
			if(!found) busPoints.push_back({pi.world.x, busY});
		}
		std::sort(busPoints.begin(), busPoints.end(),
				  [](const Avoid::Point& a, const Avoid::Point& b) {
					  return a.x < b.x;
				  });

		std::vector<Avoid::JunctionRef*> juncs;
		for(const auto& bp: busPoints) {
			auto* j = new Avoid::JunctionRef(router, bp);
			j->setPositionFixed(false);
			juncs.push_back(j);
			WB_LOG << "	 Bus " << netName << " backbone junction at ("
				   << bp.x << "," << bp.y << ")\n";
		}
		for(size_t i = 0; i + 1 < juncs.size(); ++i)
			addConnector({juncs[i]}, {juncs[i + 1]});

		for(const auto& pi: pins) {
			const size_t ni = nearestIdx(busPoints, pi.world);
			addConnector({shapeRegistry.shape(pi.comp), pi.pin},
						 {juncs[ni]});
		}
		return juncs;
	}

	/**
	 * Route signal nets.
	 */
	void routeSignals() {
		for(const auto& netName: netlist->signalNames()) {
			if(netlist->size(netName) == 2) {
				routeTwoPin(netName);
			} else {
				// routeHyperedge(netName); // failed
				routeHyperedgeStar(netName);
			}
		}
	}

	/**
	 * Route 2 pin net.
	 * @param netName net name
	 */
	void routeTwoPin(const std::string& netName) {
		const auto& nodes = netlist->pins(netName);
		addConnector({shapeRegistry.shape(nodes[0].comp), nodes[0].pin},
					 {shapeRegistry.shape(nodes[1].comp), nodes[1].pin});
	}

	/**
	 * Route hyperedge net (without aux junction), fails.
	 * @param netName net name
	 */
	void routeHyperedge(const std::string& netName) {
		const auto& nodes = netlist->pins(netName);
		Avoid::ConnEndList terminals;
		for(const auto& n: nodes) {
			terminals.push_back(Avoid::ConnEnd(shapeRegistry.shape(n.comp), n.pin));
		}
		router->hyperedgeRerouter()->registerHyperedgeForRerouting(terminals);
	}

	/**
	 * Route hyperedge net (initial warm-start hyperedge star).
	 * @param netName net name
	 * @return junction created
	 */
	Avoid::JunctionRef* routeHyperedgeStar(const std::string& netName) {
		const auto& nodes = netlist->pins(netName);
		std::vector<Avoid::Point> pts;
		for(const auto& n: nodes)
			pts.push_back(shapeRegistry.pinWorld(n.comp, n.pin));

		const Avoid::Point startPos = freeCentroid(pts);
		WB_LOG << "	 Junction " << netName << " warm-start at ("
			   << startPos.x << "," << startPos.y << ")\n";

		auto* junc = new Avoid::JunctionRef(router, startPos);
		junc->setPositionFixed(false);
		router->hyperedgeRerouter()->registerHyperedgeForRerouting(junc);

		for(const auto& n: nodes) {
			addConnector(Avoid::ConnEnd(junc), Avoid::ConnEnd(shapeRegistry.shape(n.comp), n.pin));
		}
		return junc;
	}

	bool improveJunctions(bool dryRun = false) {
		RouterState state(netRegistry);
		auto jes = state.getJunctionsToFix();
		if(jes.empty()) return false;
		WB_LOG << "Junctions to fix (" << jes.size() << "):\n";
		for(bend::JunctionEdges* je: jes) {
			std::string net = state.getNetById(je->getJunction().getId());
			bend::OccupiedArea occupied(je->getCentroid(), opts.centroidSearchRadius, opts.centroidSearchStep);
			state.buildOccupiedArea(*je, netRegistry, occupied, true);
			auto result = state.findBestJunctionPoint(*je, occupied, opts.idealNudgingDistance, true);
			WB_LOG << "\tJ[" << je->getJunction().getId() << "] net:" << net
				   << " rpos:" << bend::toStringPoint(je->getJunction()->recommendedPosition())
				   << " pos:" << bend::toStringPoint(je->getJunction()->position())
				   << " center:" << bend::toStringPoint(je->getCentroid())
				   << " best:" << bend::toStringPoint(result.point)
				   << (result.clamped ? "(could not cleanly place)" : "")
				   << " edges:";
			bool first = true;
			for(auto& ce: je->getConnectorEnds()) {
				if(!first) {
					WB_LOG << ",";
				} else {
					first = false;
				}
				WB_LOG << bend::toStringEdge(ce.getEdge())
					   << (ce.isOrthogonalCollinear() ? (ce.isOrthogonalX() ? "X!" : "Y!") : "");
			}
			if(!dryRun) {
				je->getJunction()->setPositionFixed(true);
				router->moveJunction(je->getJunction().get(), result.point);
			}
			WB_LOG << "\n";
		}
		WB_LOG << "\n";
		return true;
	}

	/**
	 * Process routing transaction.
	 */
	void processTransaction(const std::string& label) {
		WB_LOG << "\n"
			   << label << "\n";
		router->processTransaction();
		printStats();
	}

	/**
	 * Create new libavoid Connector.
	 * @param from `from` connector end
	 * @param to `to` connector end
	 * @return pointer to ConnRef created
	 */
	Avoid::ConnRef* addConnector(Avoid::ConnEnd from, Avoid::ConnEnd to) {
		Avoid::ConnRef* c = new Avoid::ConnRef(router, from, to);
		c->setRoutingType(Avoid::ConnType_Orthogonal);
		return c;
	}

	/**
	 * Find the index of vector with points nearest to the passed point.
	 * @param pts vector with points to search index
	 * @param p point to search
	 * @return index of point in `pts` nearest to `p`
	 */
	static size_t nearestIdx(const std::vector<Avoid::Point>& pts,
							 Avoid::Point p) {
		size_t best = 0;
		double bd = 1e18;
		for(size_t i = 0; i < pts.size(); ++i) {
			const double d = std::abs(pts[i].x - p.x) + std::abs(pts[i].y - p.y);
			if(d < bd) {
				bd = d;
				best = i;
			}
		}
		return best;
	}

	/**
	 * Find centroid point that is out of all components and already occupied points.
	 * @param pts points for which to find centroid
	 * @return centroid on free area
	 */
	Avoid::Point freeCentroid(const std::vector<Avoid::Point>& pts) const {
		double cx = 0, cy = 0;
		for(const auto& p: pts) {
			cx += p.x;
			cy += p.y;
		}
		cx /= pts.size();
		cy /= pts.size();

		auto isFree = [&](double x, double y) -> bool {
			return !insideAnyComp(x, y);
		};

		if(isFree(cx, cy)) return {cx, cy};
		const double step = opts.centroidSearchStep;
		for(int r = 1; r <= opts.centroidSearchRadius; ++r)
			for(int dx = -r; dx <= r; ++dx)
				for(int dy = -r; dy <= r; ++dy) {
					if(std::abs(dx) != r && std::abs(dy) != r) continue;
					const double tx = cx + dx * step, ty = cy + dy * step;
					if(isFree(tx, ty)) return {tx, ty};
				}
		WB_LOG << "WARN: Can't find free centroid, fallback centroid returned: [" << cx << "," << cy << "]\n";
		return {cx, cy}; // fallback: accept overlap
	}

	/**
	 * Check if given point is inside any of registered components.
	 * Accounts for component rotation: a 90° or 270° transform swaps w and h
	 * in world space, so the bounding box is built from effective dimensions.
	 * @param x  point x to be checked
	 * @param y  point y to be checked
	 * @return true if point is inside some component's bounding box
	 */
	bool insideAnyComp(double x, double y) const {
		for(const auto& [nm, c]: *components) {
			if(c.isInside(x, y)) {
				return true;
			}
		}
		return false;
	}

	SchematicRouterOptions opts;			// schematic router configuration options
	Avoid::Router* router = nullptr;		// libavoid router instance
	const Components* components = nullptr; // ptr to map of component name per component
	const NetList* netlist = nullptr;		// ptr to netlist

	ShapeRegistry shapeRegistry; // component, pins
	NetRegistry netRegistry;	 // registry of net (shape ID and pin per net name)
};
