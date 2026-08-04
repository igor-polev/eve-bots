/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input implementation.
*/

#include <chrono>
#include <thread>

#include <dwmapi.h>

#include "mouse_input.hpp"

namespace {

// How long the button stays down. This is not politeness: the client was
// measured redrawing at 13-41 fps, and an engine that samples input once
// per rendered frame can miss a press and release that arrive between two
// samples. 40 ms comfortably outlasts one frame at the low end.
constexpr std::chrono::milliseconds CLICK_HOLD {40};

std::string last_error_text(const char* call)
{
	return std::string(call) + " failed, error "
	     + std::to_string(GetLastError());
}

// Maps a capture frame point onto the desktop and into the client area.
bool map_point(
	HWND             window,
	const cv::Point& frame,
	cv::Point&       screen,
	cv::Point&       client,
	std::string&     error)
{
	RECT bounds {};
	const HRESULT mapped = DwmGetWindowAttribute(
		window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)
	);
	if (FAILED(mapped)) {
		error = "cannot locate the window on screen: DwmGetWindowAttribute "
		        "returned 0x" + std::to_string(static_cast<unsigned>(mapped));
		return false;
	}
	screen = cv::Point {bounds.left + frame.x, bounds.top + frame.y};

	// Client coordinates are the same desktop point measured from the top
	// left of the client area, which sits inside the frame by the border
	// and title bar.
	POINT origin {0, 0};
	if (!ClientToScreen(window, &origin)) {
		error = last_error_text("ClientToScreen");
		return false;
	}
	client = cv::Point {screen.x - origin.x, screen.y - origin.y};
	return true;
}

// Absolute SendInput coordinates are 0..65535 across the whole virtual
// desktop, not pixels, and not relative to any one monitor.
bool click_send_input(const cv::Point& screen, std::string& error)
{
	const int left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (width < 2 || height < 2) {
		error = "cannot size the virtual desktop";
		return false;
	}

	INPUT press[2] {};
	press[0].type         = INPUT_MOUSE;
	press[0].mi.dwFlags   = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE
	                      | MOUSEEVENTF_VIRTUALDESK;
	press[0].mi.dx        = static_cast<LONG>(
		(screen.x - left) * 65535LL / (width  - 1));
	press[0].mi.dy        = static_cast<LONG>(
		(screen.y - top)  * 65535LL / (height - 1));
	press[1].type         = INPUT_MOUSE;
	press[1].mi.dwFlags   = MOUSEEVENTF_LEFTDOWN;

	if (2 != SendInput(2, press, sizeof(INPUT))) {
		error = last_error_text("SendInput");
		return false;
	}
	std::this_thread::sleep_for(CLICK_HOLD);

	INPUT release {};
	release.type       = INPUT_MOUSE;
	release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	if (1 != SendInput(1, &release, sizeof(INPUT))) {
		// the button is down and staying down - say so plainly
		error = "the mouse button was pressed but "
		      + last_error_text("SendInput") + " on release";
		return false;
	}
	return true;
}

// The move first, because a window that tracks hover state has to be told
// the pointer arrived before it is told the button went down there.
bool click_post_message(HWND window, const cv::Point& client, std::string& error)
{
	const LPARAM at = MAKELPARAM(client.x, client.y);
	if (!PostMessageW(window, WM_MOUSEMOVE, 0, at) ||
		!PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at))
	{
		error = last_error_text("PostMessage");
		return false;
	}
	std::this_thread::sleep_for(CLICK_HOLD);

	if (!PostMessageW(window, WM_LBUTTONUP, 0, at)) {
		error = "the mouse button was pressed but "
		      + last_error_text("PostMessage") + " on release";
		return false;
	}
	return true;
}

} // namespace

std::string click_method_text(ClickMethod method)
{
	switch (method) {
	case ClickMethod::SEND: return "SendInput";
	case ClickMethod::POST: return "PostMessage";
	default:                return "auto";
	}
}

bool click_at(
	HWND             window,
	const cv::Point& frame,
	ClickMethod      method,
	int              wait_ms,
	ClickResult&     result,
	std::string&     error)
{
	if (!window || !IsWindow(window)) {
		error = "the captured window is gone";
		return false;
	}
	// Nothing sensible can be clicked on a window that is not on screen,
	// and capture would have stopped producing frames anyway.
	if (IsIconic(window)) {
		error = "the window is minimised";
		return false;
	}
	if (frame.x < 0 || frame.y < 0) {
		error = "cannot click " + std::to_string(frame.x) + ","
		      + std::to_string(frame.y) + ": outside the frame";
		return false;
	}

	result = ClickResult {};
	result.frame = frame;
	if (!map_point(window, frame, result.screen, result.client, error))
		return false;

	// SendInput only ever reaches the foreground window, so an unattended
	// click on a background window goes by message instead of landing in
	// whatever the user happens to be working in.
	result.method = method;
	if (ClickMethod::AUTO == method) {
		result.method = GetForegroundWindow() == window
			? ClickMethod::SEND
			: ClickMethod::POST;
	}

	const bool clicked = ClickMethod::SEND == result.method
		? click_send_input(result.screen, error)
		: click_post_message(window, result.client, error);
	if (!clicked) return false;

	if (wait_ms > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds {wait_ms});
	return true;
}
