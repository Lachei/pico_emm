#pragma once

#include <chrono>

inline uint64_t time_us_64() {
	using namespace std::chrono;
	static time_point start = system_clock::now();
	time_point now = system_clock::now();
	return duration_cast<microseconds>(now - start).count();
}

