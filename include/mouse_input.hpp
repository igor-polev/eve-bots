/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input - clicking a point of the captured window.

	Detections come back in capture frame coordinates, and the capture frame
	is the window's DWM extended frame: not GetWindowRect, which also counts
	the invisible resize border, and not the client area, which starts below
	the title bar. Both offsets are read afresh on every click, so a window
	that has moved since the detection is still clicked in the right place.

	Two ways of delivering the click, chosen per call:

	SendInput   puts the event into the system input queue, the same place
	            real hardware does, so it works however the game reads the
	            mouse. It moves the physical cursor and only reaches the
	            window that is in the foreground.
	PostMessage posts WM_LBUTTONDOWN/UP straight to the window, so it works
	            on a background window and leaves the cursor alone - but
	            only if the game reads the mouse from its message queue at
	            all, which a game that draws its own interface may not.
*/

#pragma once
#include <string>

#include <windows.h>

#include <opencv2/core.hpp>

enum class ClickMethod {
	AUTO,   // SendInput while the window is active, PostMessage when it is not
	SEND,   // SendInput, whether the window is active or not
	POST    // PostMessage, whether the window is active or not
};

// "SendInput", "PostMessage" or "auto".
std::string click_method_text(ClickMethod method);

// How long to leave the interface alone after a click. The game needs a
// moment to react before the next captured frame is worth looking at.
constexpr int UI_WAIT_DEFAULT = 20;

struct ClickResult {
	cv::Point   frame;   // the point asked for, in capture frame pixels
	cv::Point   screen;  // the same point on the desktop
	cv::Point   client;  // the same point inside the client area
	ClickMethod method {ClickMethod::SEND};   // the one actually used
};

// Clicks one point of window, given in capture frame coordinates, then
// sleeps wait_ms so the game can react before the next frame is examined.
// Returns false and fills error when the point cannot be mapped onto the
// desktop or the input is refused.
bool click_at(
	HWND             window,
	const cv::Point& frame,
	ClickMethod      method,
	int              wait_ms,
	ClickResult&     result,
	std::string&     error
);
