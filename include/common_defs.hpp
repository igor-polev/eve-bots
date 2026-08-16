/*
	EVE bots for Windows.
	Author: Igor Polev.

	Common short aliases used across the project.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace eb {

using Millis    = std::chrono::milliseconds;
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Patterns by their position in the image library. Several of them always
// mean "any of these will do".
using Images = std::vector<size_t>;

inline Millis since(TimePoint start)
{
	return std::chrono::duration_cast<Millis>(Clock::now() - start);
}

} // namespace eb
