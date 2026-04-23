#pragma once

#include "Debug.h"
#include "NetList.h"
#include "RouterShapePin.h"
#include "ShapeRegistry.h"
#include "bend/Connector.h"

#include <stdexcept>
#include <string>
#include <unordered_map>

/**
 * Registry of nets (mapping of libavoid shape id and pin (id) per net name).
 * Maintain cache of libavoid object (connection, junction) ID to net mapping.
 */
class NetRegistry {
public:
	using Shape2Pin2Net = std::map<unsigned int, std::map<unsigned int, std::string>>;

	/**
	 * Must call init(router), then registerAll()
	 */
	NetRegistry() = default;

	/**
	 * Must call registerAll()
	 */
	explicit NetRegistry(Avoid::Router* router) noexcept: router(router) {
	}

	/**
	 * Bind net registry to router (call once).
	 */
	void init(Avoid::Router* r) {
		assert(nullptr == router);
		router = r;
	}

	/**
	 * Create and populate NetRegistry.
	 * @param shapeRegistry shape registry
	 * @param rawNets raw nets
	 */
	void registerAll(const ShapeRegistry& shapeRegistry, const RawNets& rawNets) {
		assert(nullptr != router);
		for(const auto& [net, pins]: rawNets) {
			for(const auto& pin: pins) {
				registerShapePinNet(shapeRegistry.shape(pin.comp)->id(), pin.pin, net);
			}
		}
	}

	/**
	 * Register pin for net.
	 * @param shapeId shape ID
	 * @param pinNumber pin number
	 * @param netName net name
	 */
	void registerShapePinNet(unsigned int shapeId, size_t pinNumber, std::string netName) {
		shape2pin2net[shapeId][pinNumber] = netName;
	}

	/**
	 * Get net name by shape ID and pin.
	 * @param shapeId shape ID
	 * @param pin pin
	 * @return net name, empty if not found
	 */
	const std::string getNet(unsigned int shapeId, unsigned int pin) const {
		auto it = shape2pin2net.find(shapeId);
		if(it == shape2pin2net.end()) {
			WB_LOG << "WARN: Missing shape id: " << shapeId << "\n";
			return {};
		}
		const auto& pin2net = it->second;
		auto nIt = pin2net.find(pin);
		if(nIt == pin2net.end()) {
			WB_LOG << "WARN: Missing shape id: " << shapeId << ", pin id: " << pin << "\n";
			return {}; // ніт то ніт...
		}
		return nIt->second;
	}

	/**
	 * Get net name by ShapePin.
	 * @param shapePin Avoid::Shape with pin class ID
	 * @return net name, empty if not found
	 */
	const std::string getNet(const RouterShapePin& shapePin) const {
		return shapePin ? getNet(shapePin.getId(), shapePin.getPinClassId()) : "";
	}

	/**
	 * Get name by libavoid connector.
	 * @param c connector pointer
	 * @return net name, empty if not found
	 */
	std::string getNet(Avoid::ConnRef* c, int avoidObstacleId = -1) const {
		return getNet(bend::Connector(c), avoidObstacleId);
	}

	/**
	 * Get name by Connector.
	 * @param c connector
	 * @return net name, empty if not found
	 */
	std::string getNet(const bend::Connector& c, int avoidObstacleId = -1) const {
		if(!c) return {};

		auto it = id2net.find(c.getId());
		if(it != id2net.end()) return it->second;

		auto cacheNet = [&](const std::string& netName) {
			return cacheIdNet(c.getId(), netName);
		};
		auto ends = c.endpoints();

		// shape pins are faster to resolve
		RouterShapePin firstPin(ends.first);
		if(firstPin && firstPin.getId() != avoidObstacleId) {
			return cacheNet(getNet(firstPin));
		}
		RouterShapePin secondPin(ends.second);
		if(secondPin && secondPin.getId() != avoidObstacleId) {
			return cacheNet(getNet(secondPin));
		}

		bend::Junction firstJunction(ends.first);
		if(firstJunction && firstJunction.getId() != avoidObstacleId) {
			std::string netName = getNet(firstJunction, c.getId());
			if(!netName.empty()) {
				return cacheNet(netName);
			}
		}
		bend::Junction secondJunction(ends.second);
		if(secondJunction && secondJunction.getId() != avoidObstacleId) {
			return cacheNet(getNet(secondJunction, c.getId()));
		}
		return {};
	}

	/**
	 * Get net by libavoid junction.
	 * @param j junction pointer
	 * @param avoidConnectorId avoid connector ID
	 * @return net name, empty if not found
	 */
	std::string getNet(const Avoid::JunctionRef* j, int avoidConnectorId = -1) const {
		if(nullptr == j) return {};
		unsigned int jId = j->id();

		auto it = id2net.find(jId);
		if(it != id2net.end()) return it->second;

		for(Avoid::ConnRef* conn: j->attachedConnectors()) {
			if(conn->id() == avoidConnectorId) continue; // prevent infinite loop (just tree here)
			std::string net = getNet(conn, jId);
			if(!net.empty()) {
				id2net[jId] = net;
				return net;
			}
		}
		return {};
	}

	/**
	 * Get net by Junction.
	 * @param j junction
	 * @param avoidConnectorId avoid connector ID
	 * @return net name, empty if not found
	 */
	std::string getNet(const bend::Junction& j, int avoidConnectorId = -1) const {
		return getNet(j.get(), avoidConnectorId);
	}

	/**
	 * Get net by libavoid object ID
	 * @param id libavoid object ID
	 * @return net name if found, otherwise empty string
	 */
	inline std::string getNetById(unsigned int id) const {
		auto it = id2net.find(id);
		return it == id2net.end() ? "" : it->second;
	}

	/**
	 * Update ID to net name cache.
	 * @param id libavoid object ID
	 * @param netName net name
	 */
	inline const std::string& cacheIdNet(unsigned int id, const std::string& netName) const {
		if(!netName.empty()) {
			id2net[id] = netName;
		}
		return netName;
	}

	/**
	 * Is this connection valid?
	 * @return true if this connection valid
	 */
	bool isValid(const Avoid::ConnRef* c) const {
		for(const Avoid::ConnRef* a: router->connRefs) {
			if(c == a) return true;
		}
		return false;
	}

	/**
	 * Is this connector valid?
	 * @return true if this connection valid
	 */
	bool isValid(const bend::Connector& c) const {
		return isValid(c.get());
	}

	/**
	 * Is this junction valid?
	 */
	bool isValid(const Avoid::JunctionRef* j) const {
		for(const Avoid::Obstacle* o: router->m_obstacles) {
			if(o == j) return true;
		}
		return false;
	}

	/**
	 * @brief get Obstacle list (shapes and junctions) from router
	 */
	const Avoid::ObstacleList& getObstacles() const {
		return router->m_obstacles;
	}

	/**
	 * @brief get ConnRef list (connections) from router
	 */
	const Avoid::ConnRefList& getConnections() const {
		return router->connRefs;
	}

	/**
	 * @brief Router implementation
	 */
	Avoid::Router* getRouter() const {
		return router;
	}

private:
	Avoid::Router* router;
	Shape2Pin2Net shape2pin2net;
	mutable std::unordered_map<unsigned int, std::string> id2net;
};