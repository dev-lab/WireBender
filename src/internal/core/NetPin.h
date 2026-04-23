#pragma once

#include <string>

/**
 * Raw component pin, used by NetList.
 */
struct NetPin {
	std::string comp; // component name
	unsigned int pin; // pin number
};
