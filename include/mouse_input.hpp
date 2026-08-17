/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input - clicking a point of the captured window.

	Frame coordinates are measured from the window's DWM extended frame. Not
	from GetWindowRect, which also counts the invisible resize border, and
	not from the client area, which starts below the title bar. The offset is
	read again on every click, so a window that moved after the detection is
	still clicked in the right place.

	Clicks go through SendInput. Posting WM_LBUTTONDOWN/UP was tried and
	removed: EVE draws its own interface and ignores posted mouse messages.
	Do not bring it back without testing against the client.

	So a click has to arrange three things itself, and none of them can be
	left out. The game must be in front, or WM_MOUSEACTIVATE eats the click
	to activate the window. Nothing may lie over the point on screen, because
	the click lands on whatever is on top there. And the cursor and the old
	focus have to be put back afterwards. click_at() does all three, and
	first waits for a gap in the user's work; InputPriority says how long it
	waits.
*/

#pragma once

#include <string>

#include <windows.h>

#include <opencv2/core.hpp>

#include "common_defs.hpp"
#include "input_priority.hpp"

// How long to leave the interface alone after a click. The game needs a
// moment to react before the next frame is worth looking at.
constexpr eb::Millis UI_WAIT_DEFAULT {20};

struct ClickResult {
	cv::Point frame;    // the point asked for, in capture frame pixels
	cv::Point screen;   // the same point on the desktop
	bool activated {false};   // the window had to be brought to the front
	bool restored  {false};   // the focus was handed back afterwards
	bool yielded   {false};   // the desktop was in use and was waited for
};

// Clicks one point of window, given in capture frame coordinates, then
// sleeps for wait so the game can react before the next frame is read.
// Returns false and fills error when the point cannot be mapped onto the
// desktop, when the input is refused, or when the desktop never became free
// within the priority timeout. A click always runs to its end: it takes a
// quarter of a second and cannot be left half done.
bool click_at(
	HWND                 window,
	const cv::Point&     frame,
	eb::Millis           wait,
	const InputPriority& priority,
	ClickResult&         result,
	std::string&         error
);
