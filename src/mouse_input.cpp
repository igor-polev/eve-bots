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

// Gap between putting the cursor on the target and pressing the button.
// The two used to travel in one SendInput batch, arriving in the same
// instant, which left the game no frame in which to notice the pointer had
// arrived - a control that highlights on hover never saw the hover.
constexpr std::chrono::milliseconds MOVE_SETTLE {80};

// Activation is asynchronous, so it has to be waited for rather than
// assumed, and a window that has just come forward needs a moment before
// it is listening again.
constexpr std::chrono::milliseconds FOCUS_TIMEOUT {1500};
constexpr std::chrono::milliseconds FOCUS_POLL    {25};
constexpr std::chrono::milliseconds FOCUS_SETTLE  {150};

std::string last_error_text(const char* call)
{
	return std::string(call) + " failed, error "
	     + std::to_string(GetLastError());
}

// Maps a capture frame point onto the desktop.
bool map_point(
	HWND             window,
	const cv::Point& frame,
	cv::Point&       screen,
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
	return true;
}

// Name of whatever window owns a point on the desktop, for the message
// that explains a refused click.
std::string window_name(HWND window)
{
	wchar_t title[128] {};
	const int length = GetWindowTextW(window, title, 128);
	if (length <= 0) return "another window";

	// The console is UTF-8 and these titles are only ever shown to a
	// person, so a lossy narrowing is good enough here.
	std::string name;
	for (int i = 0; i < length; ++i) {
		const wchar_t c = title[i];
		name += (c < 32 || c > 126) ? '?' : static_cast<char>(c);
	}
	return "'" + name + "'";
}

// Brings a window to the front and waits until it really is there.
bool bring_to_front(HWND window, bool& activated, std::string& error)
{
	activated = false;
	if (GetForegroundWindow() == window) return true;

	activated = true;
	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);

	// Windows only lets the process that already owns the foreground, or
	// that supplied the last input, hand it to somebody else; everyone
	// else just gets a flashing taskbar button. Attaching our input queue
	// to the foreground window's thread makes Windows treat the two as
	// one thread, which is the documented way past that rule.
	const HWND  front  = GetForegroundWindow();
	const DWORD ours   = GetCurrentThreadId();
	const DWORD theirs = front ? GetWindowThreadProcessId(front, nullptr) : 0;
	const bool  shared = theirs && theirs != ours;
	if (shared) AttachThreadInput(ours, theirs, TRUE);

	SetForegroundWindow(window);
	BringWindowToTop(window);

	if (shared) AttachThreadInput(ours, theirs, FALSE);

	for (std::chrono::milliseconds waited {0};
	     waited < FOCUS_TIMEOUT;
	     waited += FOCUS_POLL)
	{
		if (GetForegroundWindow() == window) {
			// Being in front is not the same as being ready for input: the
			// game picks its input handling back up on the next frame.
			std::this_thread::sleep_for(FOCUS_SETTLE);
			return true;
		}
		std::this_thread::sleep_for(FOCUS_POLL);
	}

	error = "the window would not come to the front, and a click on an "
	        "inactive window is swallowed to activate it instead of acting";
	return false;
}

} // namespace

bool click_at(
	HWND             window,
	const cv::Point& frame,
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
	if (!map_point(window, frame, result.screen, error)) return false;

	// Before anything else, because an inactive window eats the click that
	// activates it, and because this changes what is on top at the point
	// the occlusion check below looks at.
	if (!bring_to_front(window, result.activated, error)) return false;

	// The window moves while it is coming forward on some setups, so the
	// point is mapped again now that it has settled.
	if (!map_point(window, frame, result.screen, error)) return false;

	// Capture sees through whatever is covering the game, so a pattern can
	// be found at a point that on screen belongs to somebody else. Clicking
	// it would press a button in that application instead.
	const POINT at {result.screen.x, result.screen.y};
	const HWND  owner = GetAncestor(WindowFromPoint(at), GA_ROOT);
	if (owner != window) {
		error = "cannot click " + std::to_string(result.screen.x) + ","
		      + std::to_string(result.screen.y) + ": "
		      + (owner ? window_name(owner) : std::string("another window"))
		      + " is on top there even after raising the game";
		return false;
	}

	// Absolute SendInput coordinates are 0..65535 across the whole virtual
	// desktop, not pixels, and not relative to any one monitor.
	const int left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (width < 2 || height < 2) {
		error = "cannot size the virtual desktop";
		return false;
	}

	// Move, press and release go one at a time with a pause between, so
	// each lands in a different rendered frame. Sent together they arrive
	// in the same instant and the game can miss the pointer ever being
	// there.
	INPUT move {};
	move.type       = INPUT_MOUSE;
	move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE
	                | MOUSEEVENTF_VIRTUALDESK;
	move.mi.dx      = static_cast<LONG>(
		(result.screen.x - left) * 65535LL / (width  - 1));
	move.mi.dy      = static_cast<LONG>(
		(result.screen.y - top)  * 65535LL / (height - 1));
	if (1 != SendInput(1, &move, sizeof(INPUT))) {
		error = last_error_text("SendInput") + " on move";
		return false;
	}
	std::this_thread::sleep_for(MOVE_SETTLE);

	INPUT press {};
	press.type       = INPUT_MOUSE;
	press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	if (1 != SendInput(1, &press, sizeof(INPUT))) {
		error = last_error_text("SendInput") + " on press";
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

	if (wait_ms > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds {wait_ms});
	return true;
}
