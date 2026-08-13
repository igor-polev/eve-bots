/*
	EVE bots for Windows.
	Author: Igor Polev.

	Frame - a single captured screen image.
*/

#pragma once

#include <cstdint>
#include <vector>

#include "common_defs.hpp"

// Tightly packed 32-bit BGRA pixels, top-down row order.
struct Frame {
	std::vector<uint8_t> pixels;
	uint32_t width  {0};
	uint32_t height {0};
	// When these pixels were captured. Doubles as the frame's identity: two
	// searches handed the same reading are looking at the very same pixels,
	// so they cannot honestly come to different answers.
	eb::TimePoint taken {};

	bool empty() const noexcept {
		return pixels.empty() || 0 == width || 0 == height;
	}
	uint32_t stride() const noexcept {
		return width * 4;
	}
};
