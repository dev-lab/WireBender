#pragma once

/**
 * Options to configure SchematicRouter.
 */
struct SchematicRouterOptions {

	// Component / pin geometry — inside shape offset routing, passed to ShapeConnectionPin.
	double pinInsideOffset = 0.0;

	// libavoid routing penalties
	double segmentPenalty = 50.0; // default: 50; shall be > 0
	double anglePenalty = 0.0; // default: 0
	double crossingPenalty = 200.0; // default: 200
	double clusterCrossingPenalty = 4000.0; // default: 4000
	double fixedSharedPathPenalty = 0.0; // in tests: 9000, 0 needed for optimization
	double portDirectionPenalty = 100.0; // default: 100

	// libavoid routing parameters
	double idealNudgingDistance = 8.0;
	double shapeBufferDistance = 8.0; // was 4

	// libavoid routing options
	bool nudgeOrthogonalSegmentsConnectedToShapes = true; // default: false
	bool penaliseOrthogonalSharedPathsAtConnEnds = true;
	bool nudgeOrthogonalTouchingColinearSegments = true;
	bool improveHyperedgeRoutesMovingAddingAndDeletingJunctions = true;

	// Bus backbone layout
	double busTopBase = 10.0;		   // Y of the topmost positive rail
	double busTopSpacing = 50.0;	   // spacing between stacked positive rails
	double busBotMargin = 60.0;		   // gap below the lowest component
	double busBotSpacing = 50.0;	   // spacing between stacked ground/neg rails
	double busCollapseThreshold = 5.0; // merge backbone junctions closer than this

	// Hyperedge warm-start: freeCentroid spiral search
	int centroidSearchRadius = 60;
	double centroidSearchStep = 4.0;

	// Minimum distance between any two hyperedge warm-start junction centres.
	// Must be large enough that libavoid routing between nearby junctions
	// does not produce wires that visually cross unrelated nets.
	// Rule of thumb: at least 3-4x idealNudgingDistance (default 8px).
	double minJunctionSeparation = 40.0;

	// Net-labeller spatial fuzz radius (integer world-coordinate units)
	int spatialFuzz = 4;

	// Maximum number of attempts to improve junctions
	int maxImproveJunctionAttempts = 3;
};
