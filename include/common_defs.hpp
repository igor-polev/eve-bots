/*
	EVE bots for Windows.
	Author: Igor Polev.

	Common short aliases used across the project.
*/

#pragma once

#include <chrono>

namespace eb {

using Millis    = std::chrono::milliseconds;
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline Millis since(TimePoint start)
{
	return std::chrono::duration_cast<Millis>(Clock::now() - start);
}

} // namespace eb
