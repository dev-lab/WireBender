#pragma once

#include "libavoid/libavoid.h"
#include <string>

/**
 * Component Pin.
 */
struct Pin {
	unsigned int number;	 // Pin number
	std::string name;		 // Pin name
	double x;				 // Pin x coordinate, relative to component top-left corner
	double y;				 // Pin y coordinate, relative to component top-left corner
	Avoid::ConnDirFlags dir; // pin direction(s), refer Avoid::ConnDirFlag
};
