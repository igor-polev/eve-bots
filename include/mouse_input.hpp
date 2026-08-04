/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input - clicking a point of the captured window.

	Detections come back in capture frame coordinates, and the capture frame
	is the window's DWM extended frame: not GetWindowRect, which also counts
	the invisible resize border, and not the client area, which starts below
	the title bar. The offset is read afresh on every click, so a window
	that has moved since the detection is still clicked in the right place.

	Clicks go through SendInput, which puts the event into the system input
	queue where real hardware puts it. Posting WM_LBUTTONDOWN/UP to the
	window instead was tried and removed: EVE draws its own interface and
	ignores posted mouse messages entirely, so it looked like a click that
	did nothing. Do not reintroduce it without testing against the client.

	A click only counts when the game already has the foreground. Clicking
	an inactive window makes Windows ask it about WM_MOUSEACTIVATE first,
	and a game that answers MA_ACTIVATEANDEAT swallows that click to become
	active - which looks exactly like the cursor moving to the right place
	and nothing happening. Games also read the mouse through DirectInput or
	Raw Input, neither of which delivers to a window that is not in front.
	So click_at() brings the window to the front and waits for that to take
	effect before it presses anything, and gives up rather than clicking
	into a window that would only eat it.

	Bringing the game forward is not optional and it does take the focus
	away from whatever else is running. With PostMessage gone there is no
	way to click a background window at all.

	The other cost of SendInput is that the click lands wherever the cursor
	is put, on whichever window is on top there. Capture keeps working when
	the game is covered by something else, so a detection can succeed while
	the point on screen belongs to another application - and the click
	would land in it. click_at() refuses in that case too.
*/

#pragma once
#include <string>

#include <windows.h>

#include <opencv2/core.hpp>

// How long to leave the interface alone after a click. The game needs a
// moment to react before the next captured frame is worth looking at.
constexpr int UI_WAIT_DEFAULT = 20;

struct ClickResult {
	cv::Point frame;    // the point asked for, in capture frame pixels
	cv::Point screen;   // the same point on the desktop
	bool activated {false};   // the window had to be brought to the front
};

// Clicks one point of window, given in capture frame coordinates, then
// sleeps wait_ms so the game can react before the next frame is examined.
// Brings the window to the front first; see the note above for why that
// is not optional.
// Returns false and fills error when the point cannot be mapped onto the
// desktop, the window will not come forward, something else is covering
// the point, or the input is refused.
bool click_at(
	HWND             window,
	const cv::Point& frame,
	int              wait_ms,
	ClickResult&     result,
	std::string&     error
);
