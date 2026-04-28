/*
 * Copyright (c) 2026 Taras Greben
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
 * See LICENSE file for details.
 */

#pragma once

#include "Component.h"
#include "Debug.h"
#include "bend/utils.h"

#include <map>
#include <stdexcept>
#include <string>

/**
 * Translates the schematic Component map into libavoid ShapeRef and
 * ShapeConnectionPin objects, and provides the pinWorld() query.
 *
 * This is a pure adapter: it knows about Component/Pin (schematic model) and
 * libavoid shapes, but nothing about nets, routing, or SVG.
 *
 * Transform convention (matches libavoid move order):
 *   1. flipX  — mirror about the vertical centre axis  (x → w - x)
 *   2. rotate — clockwise, in 90° steps
 *
 * Pin coordinates stored in Pin are always relative to the component's
 * unrotated, unflipped top-left corner.
 *
 * registerComp() registers the shape and its pins in the original local
 * coordinate system and then calls ShapeRef::transformConnectionPinPositions()
 * so that libavoid applies the transform internally.  The manual
 * helper transformPinLocal() exists solely for calculations
 * that happen outside of libavoid, such as pinWorld().
 */
class ShapeRegistry {
public:
	using Comps = std::map<std::string, Component>;
	using ShapeMap = std::map<std::string, Avoid::ShapeRef*>;
	using ShapeIdMap = std::map<unsigned int, std::string>;

	/**
	 * Must call init(), register*().
	 */
	ShapeRegistry() noexcept = default;

	/**
	 * Create Shape Registry, must call register*().
	 * @param router the libavoid Router that will own the shapes
	 * @param insideOffset inside shape offset routing, passed to ShapeConnectionPin
	 */
	ShapeRegistry(Avoid::Router* router,
				  double insideOffset = 0.0) noexcept
		: router(router), insideOffset(insideOffset) {}

	/**
	 * Bind shape registry to router (call once).
	 */
	void init(Avoid::Router* r, double iOffset = 0.0) {
		assert(nullptr == router);
		router = r;
		insideOffset = iOffset;
	}

	/**
	 * Register all components. Safe to call exactly once.
	 * @param comps map of component name to component object
	 */
	void registerAll(const Comps& cmps) {
		comps = &cmps;
		for(const auto& [name, comp]: cmps) {
			registerComp(name, comp);
		}
	}

	/**
	 * Get shape by component name.
	 * @param comp component name
	 * @return pointer to ShapeRef
	 */
	Avoid::ShapeRef* shape(const std::string& comp) const {
		return shapes.at(comp);
	}

	/**
	 * Get world coordinate of a component pin.
	 * Pin local coords (relative to unrotated/unflipped top-left) are
	 * transformed through the component's flipX then rotation, then offset
	 * by the component's world-space top-left corner.
	 * @param comp component name
	 * @param pin  pin number
	 * @return world coordinate of the pin
	 */
	Avoid::Point pinWorld(const std::string& comp, unsigned int pin) const {
		const Component& c = comps->at(comp);
		for(const auto& p: c.pins) {
			if(p.number != pin) continue;

			const auto [lx, ly] = transformPinLocal(p.x, p.y, c);

			// World top-left of the (possibly rotated) bounding box.
			const double cx = c.placement.position.x;
			const double cy = c.placement.position.y;
			const double ew = c.getWidth();
			const double eh = c.getHeight();
			return Avoid::Point(cx - ew / 2.0 + lx,
								cy - eh / 2.0 + ly);
		}
		throw std::runtime_error("pin not found: " + comp + "." + std::to_string(pin));
	}

	Avoid::Point pinWorld(const Avoid::ConnEnd& connEnd) const {
		if(connEnd.type() == Avoid::ConnEndShapePin) {
			Avoid::ShapeRef* shape = connEnd.shape();
			if(shape) {
				std::string name = getShapeName(shape->id());
				if(!name.empty()) {
					return pinWorld(name, connEnd.pinClassId());
				}
			}
		}
		return connEnd.type() == Avoid::ConnEndJunction ? connEnd.junction()->recommendedPosition() : connEnd.position();
	}

	/**
	 * Get all registered shapes.
	 * @return map of name to shape
	 */
	const ShapeMap& getShapes() const {
		return shapes;
	}

	/**
	 * Get shape IDs map.
	 * @return map of shape id per component name
	 */
	const ShapeIdMap& getShapeIds() const {
		return shapeIds;
	}

	/**
	 * Get shape name by ID.
	 * @param id shape ID
	 * @return shape name
	 */
	std::string getShapeName(unsigned int id) const {
		ShapeIdMap::const_iterator it = getShapeIds().find(id);
		return it == getShapeIds().end() ? "" : it->second;
	}

	/**
	 * Register a single component as a libavoid ShapeRef.
	 *
	 * The shape polygon and all ShapeConnectionPin offsets are expressed in
	 * the component's original (unrotated, unflipped) coordinate system.
	 * After all pins are attached, ShapeRef::transformConnectionPinPositions()
	 * is called with the component's transform so that libavoid repositions
	 * the shape and pins in world space internally — no manual coordinate
	 * transformation is needed here.
	 *
	 * @param name component name
	 * @param comp component object
	 * @return pointer to ShapeRef
	 */
	Avoid::ShapeRef* registerComp(const std::string& name, const Component& comp) {
		const double cx = comp.placement.position.x;
		const double cy = comp.placement.position.y;

		// Build the polygon in the unrotated/unflipped coordinate system.
		// libavoid will reposition it via transformConnectionPinPositions().
		Avoid::Polygon poly(Avoid::Rectangle(
				Avoid::Point(cx - comp.w / 2.0, cy - comp.h / 2.0),
				Avoid::Point(cx + comp.w / 2.0, cy + comp.h / 2.0)));
		Avoid::ShapeRef* shapeRef = nullptr;
		shapes[name] = shapeRef = new Avoid::ShapeRef(router, poly);
		shapeIds[shapeRef->id()] = name;
		WB_LOG << "Component[" << shapeRef->id() << "] name: " << name
			   << " (" << comp.w << " X " << comp.h << ")"
			   << " at: " << bend::toStringPoint(comp.placement.position) << ", pins:\n";

		// Avoid libavoid warnings when creating ShapeConnectionPin due to
		// floating-point arithmetic issues near 0 and max extents.
		auto clampPinPos = [](double pos, double max) -> double {
			return bend::is0(pos)		   ? Avoid::ATTACH_POS_MIN_OFFSET
				   : bend::equal(pos, max) ? Avoid::ATTACH_POS_MAX_OFFSET
										   : pos;
		};
		auto isCenter = [](double xf, double w, double yf, double h) -> bool {
			return bend::equal(xf * 2.0, w) && bend::equal(yf * 2.0, h);
		};

		// Register pins in the original (unrotated/unflipped) local coordinate
		// system.  Fractional offsets are relative to the unrotated comp.w × comp.h
		// bounding box, matching the polygon built above.
		for(const auto& pin: comp.pins) {
			if(isCenter(pin.x, comp.w, pin.y, comp.h)) {
				WB_LOG << "\tpin[" << pin.number << "]: " << pin.name
					   << " at: " << bend::toStringPoint(comp.placement.position)
					   << " dir: " << bend::toStringDirection(pin.dir) << " (center)\n";
				new Avoid::ShapeConnectionPin(
						shapeRef, pin.number,
						Avoid::ATTACH_POS_CENTRE, Avoid::ATTACH_POS_CENTRE,
						pin.x, pin.dir);
			} else {
				const double xf = clampPinPos(pin.x, comp.w);
				const double yf = clampPinPos(pin.y, comp.h);
				WB_LOG << "\tpin[" << pin.number << "]: " << pin.name
					   << " local: " << bend::toStringPoint(xf, yf) << " (org: " << bend::toStringPoint(pin)
					   << ") frac: " << bend::toStringPoint(pin.x / comp.w, pin.y / comp.h)
					   << " dir: " << bend::toStringDirection(pin.dir) << "\n";
				new Avoid::ShapeConnectionPin(
						shapeRef, pin.number,
						xf, yf, false,
						insideOffset, pin.dir);
			}
		}

		if(!comp.placement.transform.isEmpty()) {
			transform(shapeRef, bend::Transform(), comp.placement.transform);
			WB_LOG << "\tTransformed flipX: " << comp.placement.transform.isFlipX()
				   << " rotation: " << comp.placement.transform.getRotation() << "\n";
		}

		return shapeRef;
	}

	/**
	 * @brief Transform Shape (flip/rotate).
	 * @param shapeRef ShapeRef to be transformed
	 * @param current current transform state
	 * @param target target transform state
	 */
	bool transform(Avoid::ShapeRef* shapeRef,
				   const bend::Transform& current,
				   const bend::Transform& target) {
		if(current.getRotation() == target.getRotation() && current.isFlipX() == target.isFlipX())
			return false;

		const bool flipChanged = current.isFlipX() != target.isFlipX();
		const int rotDelta = (target.getRotation() - current.getRotation() + 360) % 360;

		// Step 1: flip delta first, using current box.
		// At a quarter rotation the box dims are swapped (h×w),
		// so FlipX in original space = FlipY in current box space.
		if(flipChanged) {
			const bool quarterRotation = current.isQuarterRotation();
			shapeRef->transformConnectionPinPositions(
					quarterRotation ? Avoid::TransformationType_FlipY
									: Avoid::TransformationType_FlipX);
		}

		// Step 2: rotation delta — always correct against whatever box we're now in.
		if(rotDelta != 0) {
			shapeRef->transformConnectionPinPositions(toRotationEnum(rotDelta));
		}

		// Step 3: shape polygon — pure vertex math, undo+apply is fine here.
		transformShape(shapeRef, current, target);
		return true;
	}

private:
	/**
	 * @brief Rotation degree to libavoid rotation enum.
	 */
	static inline Avoid::ShapeTransformationType toRotationEnum(int rotation) {
		return 270 == rotation	 ? Avoid::TransformationType_CW270
			   : 180 == rotation ? Avoid::TransformationType_CW180
								 : Avoid::TransformationType_CW90;
	};

	/**
	 * @brief Rotate shape to match shape pin rotation
	 * (missing libavoid API, while flipY removed through normalisation earlier).
	 * @param shape shape polygon to be transformed
	 * @param current already applied transform
	 * @param target target transform
	 */
	void transformShape(Avoid::ShapeRef* shape,
						const bend::Transform& current,
						const bend::Transform& target) {
		const Avoid::Polygon& poly = shape->polygon();
		if(poly.empty()) return;
		const Avoid::Point center = shape->position();
		Avoid::Polygon result((int)poly.size());

		const bool flipChanged = current.isFlipX() != target.isFlipX();
		const int rotDelta = (target.getRotation() - current.getRotation() + 360) % 360;
		const bool quarterRotation = current.isQuarterRotation();

		for(size_t i = 0; i < poly.size(); ++i) {
			Avoid::Point p = poly.at(i);

			// Step 1: flip delta in current box space.
			// At quarter rotation the component X axis is world Y axis,
			// so FlipX in component space = FlipY in world space.
			if(flipChanged) {
				if(quarterRotation)
					p.y = 2.0 * center.y - p.y;
				else
					p.x = 2.0 * center.x - p.x;
			}

			if(rotDelta != 0) {
				const double x = p.x - center.x;
				const double y = p.y - center.y;
				switch(rotDelta) {
				case 90:
					p = {center.x - y, center.y + x};
					break;
				case 180:
					p = {center.x - x, center.y - y};
					break;
				case 270:
					p = {center.x + y, center.y - x};
					break;
				}
			}
			result.setPoint(i, p);
		}
		router->moveShape(shape, result);
	}

	/**
	 * Apply the component's transform (flipX then rotation) to a pin's local
	 * position and return the resulting position in the world-space bounding
	 * box coordinate system ([0, ew] × [0, eh]).
	 *
	 * Used only for manual world-space calculations (e.g. pinWorld()).
	 * libavoid registration uses ShapeRef::transformConnectionPinPositions()
	 * instead, so no manual direction or coordinate transform is needed there.
	 * @param px   pin x in unflipped/unrotated local space
	 * @param py   pin y in unflipped/unrotated local space
	 * @param comp component whose transform to apply
	 * @return {lx, ly} in world-space bounding box coords
	 */
	inline static std::pair<double, double> transformPinLocal(double px, double py,
															  const Component& comp) {
		const double w = comp.w;
		const double h = comp.h;

		// Step 1: flipX — mirror about the vertical centre axis.
		if(comp.placement.transform.isFlipX()) {
			px = w - px;
		}

		// Step 2: clockwise rotation.
		switch(comp.placement.transform.getRotation()) {
		case 90:
			return {h - py, px};
		case 180:
			return {w - px, h - py};
		case 270:
			return {py, w - px};
		default:
			return {px, py}; // 0°
		}
	}

	Avoid::Router* router = nullptr; // router: owns shapes and pins
	const Comps* comps = nullptr;	 // components to translate to shapes
	double insideOffset = 0.0;		 // inside shape offset routing, passed to ShapeConnectionPin
	ShapeMap shapes;				 // map of component name to registered shape
	ShapeIdMap shapeIds;			 // map of global shape ID to component name
};
