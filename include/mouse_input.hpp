/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input - clicking a point of the captured window.

	Frame coordinates are measured from the window's DWM extended frame, not
	GetWindowRect, which also counts the invisible resize border, and not the
	client area, which starts below the title bar. The offset is read afresh
	on every click, so a window that has moved since the detection is still
	clicked in the right place.

	Clicks go through SendInput. Posting WM_LBUTTONDOWN/UP was tried and
	removed: EVE draws its own interface and ignores posted mouse messages
	entirely. Do not reintroduce it without testing against the client.

	That leaves three things a click has to arrange for itself, none of them
	optional: the game must hold the foreground, or WM_MOUSEACTIVATE eats the
	click to activate it; nothing may lie over the point on screen, since the
	click lands on whatever is on top there; and the cursor and the previous
	focus have to be put back afterwards. click_at() does all three, and
	waits for a gap in what the user is doing first - InputPriority says how
	patiently.
*/

#pragma once

#include <functional>
#include <string>

#include <windows.h>

#include <opencv2/core.hpp>

#include "input_priority.hpp"

// How long to leave the interface alone after a click. The game needs a
// moment to react before the next captured frame is worth looking at.
constexpr int UI_WAIT_DEFAULT = 20;

struct ClickResult {
	cv::Point frame;    // the point asked for, in capture frame pixels
	cv::Point screen;   // the same point on the desktop
	bool activated {false};   // the window had to be brought to the front
	bool restored  {false};   // the focus was handed back afterwards
	bool yielded   {false};   // the desktop was in use and was waited for
};

// Asked between waits, so that a program told to stop is not held here for
// as long as the user keeps typing. May be empty, and is then never asked.
using InputStop = std::function<bool()>;

// Clicks one point of window, given in capture frame coordinates, then
// sleeps wait_ms so the game can react before the next frame is examined.
// Returns false and fills error when the point cannot be mapped onto the
// desktop, the input is refused, a stop was asked for, or the desktop was
// never free enough within the priority's timeout.
bool click_at(
	HWND                 window,
	const cv::Point&     frame,
	int                  wait_ms,
	const InputPriority& priority,
	const InputStop&     stopping,
	ClickResult&         result,
	std::string&         error
);
