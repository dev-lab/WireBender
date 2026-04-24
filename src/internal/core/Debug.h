#pragma once

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string makeTimestampFilename() {
	auto now = std::chrono::system_clock::now();

	// Split into seconds and milliseconds
	auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();

	// Convert to time_t for tm
	std::time_t tt = std::chrono::system_clock::to_time_t(seconds);
	std::tm tm = *std::localtime(&tt);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d_%H%M%S")
		<< "_" << std::setw(3) << std::setfill('0') << ms;

	return oss.str(); // e.g. 20260330_142355_123
}

namespace bend {
struct NullBuf: std::streambuf {
	int overflow(int c) override {
		return c;
	}
};
inline NullBuf g_nullBuf;
inline std::ostream g_nullStream(&g_nullBuf);
} // namespace bend

#ifndef WB_DEBUG
#ifndef NDEBUG
#define WB_DEBUG 1
#endif
#endif

#ifndef WB_DEBUG
#define WB_LOG bend::g_nullStream
#else
#define WB_LOG std::cout
#endif
