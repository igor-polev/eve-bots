/*
	EVE bots for Windows.
	Author: Igor Polev.

	Mouse input implementation.
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <dwmapi.h>

#include "mouse_input.hpp"

namespace {

using Clock = std::chrono::steady_clock;

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

// How often the desktop is looked at while waiting for it to be free.
// Short enough to catch the first gap in what the user is doing, and to
// let a program that was told to stop go promptly.
constexpr std::chrono::milliseconds YIELD_POLL {50};

// GetLastInputInfo counts injected input as input, so our own clicks look
// exactly like the user being busy: after one click the desktop would
// appear to be in use for the whole idle time, every time, entirely by
// ourselves. The tick of the last event we sent is therefore remembered,
// and anything no newer than that is not the user.
std::atomic<DWORD> g_our_input  {0};
std::atomic<DWORD> g_user_input {0};
std::atomic<bool>  g_user_known {false};

void note_our_input() { g_our_input.store(GetTickCount()); }

// How long since the user last touched the mouse or the keyboard. Ticks
// wrap round every 49 days, so the comparisons are made on signed
// differences rather than on the values themselves.
std::chrono::milliseconds user_quiet_for()
{
	LASTINPUTINFO info {};
	info.cbSize = sizeof(info);
	// Not known ever to fail; if it somehow does, the answer that leaves
	// programs working is that nobody is using the desktop.
	if (!GetLastInputInfo(&info)) return std::chrono::hours {24};

	const DWORD ours = g_our_input.load();
	const bool  mine = 0 != ours
	                && static_cast<LONG>(info.dwTime - ours) <= 0;
	if (!mine || !g_user_known.load()) {
		g_user_input.store(info.dwTime);
		g_user_known.store(true);
	}

	const LONG quiet =
		static_cast<LONG>(GetTickCount() - g_user_input.load());
	return std::chrono::milliseconds {std::max<LONG>(0, quiet)};
}

// Waits for a gap in what the user is doing. False means a stop was asked
// for; running out of time is not a failure but the end of the waiting -
// stalling a program indefinitely is worse than one interrupted keystroke,
// so the caller goes ahead.
bool wait_for_quiet(
	std::chrono::milliseconds needed,
	Clock::time_point         deadline,
	const InputStop&          stopping,
	bool&                     yielded)
{
	if (needed <= std::chrono::milliseconds::zero()) return true;

	while (user_quiet_for() < needed) {
		if (stopping && stopping()) return false;
		if (Clock::now() >= deadline) return true;
		yielded = true;
		std::this_thread::sleep_for(YIELD_POLL);
	}
	return true;
}

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

// Asks for a window to be brought forward. Does not wait for it.
//
// Windows only lets the process that already owns the foreground, or that
// supplied the last input, hand it to somebody else; everyone else just
// gets a flashing taskbar button. Attaching our input queue to the
// foreground window's thread makes Windows treat the two as one thread,
// which is the documented way past that rule.
void raise_window(HWND window)
{
	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);

	const HWND  front  = GetForegroundWindow();
	const DWORD ours   = GetCurrentThreadId();
	const DWORD theirs = front ? GetWindowThreadProcessId(front, nullptr) : 0;
	const bool  shared = theirs && theirs != ours;
	if (shared) AttachThreadInput(ours, theirs, TRUE);

	SetForegroundWindow(window);
	BringWindowToTop(window);

	if (shared) AttachThreadInput(ours, theirs, FALSE);
}

// Puts the desktop back the way it was found - the cursor where it was and
// the focus on whatever held it - however click_at() ends. A destructor
// rather than a call at the end, so a click refused after the game was
// raised does not walk off with the focus it took.
class Desktop {
public:
	Desktop(const POINT& cursor, HWND front, bool* restored)
		: m_cursor {cursor}, m_front {front}, m_restored {restored} {}
	Desktop(const Desktop&)            = delete;
	Desktop& operator=(const Desktop&) = delete;

	~Desktop()
	{
		SetCursorPos(m_cursor.x, m_cursor.y);
		// Putting the cursor back is input too, and would otherwise read
		// back as the user having just moved the mouse.
		note_our_input();

		// Nothing to give back if the game already had the focus, or if
		// whatever held it has since closed.
		if (!m_front || !IsWindow(m_front)) return;
		if (GetForegroundWindow() == m_front) return;

		raise_window(m_front);
		if (m_restored) *m_restored = true;
	}

private:
	POINT m_cursor {};
	HWND  m_front {nullptr};
	bool* m_restored {nullptr};
};

// Brings a window to the front and waits until it really is there.
bool bring_to_front(HWND window, bool& activated, std::string& error)
{
	activated = false;
	if (GetForegroundWindow() == window) return true;

	activated = true;
	raise_window(window);

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

// One go at the disturbing part: raise the game, make sure the point on
// screen still belongs to it, click it. The desktop is put back before
// this returns however it ends, so an attempt that could not be made does
// not sit on the user's focus while the next one is waited for.
//
// busy says the desktop was in somebody else's hands - the game would not
// come forward, or another window was over the point. Those are worth
// trying again; everything else here is worth reporting.
bool try_click(
	HWND             window,
	const cv::Point& frame,
	ClickResult&     result,
	bool&            busy,
	std::string&     error)
{
	busy = false;

	// Taken before anything is disturbed, so it can all be put back.
	POINT cursor_was {};
	GetCursorPos(&cursor_was);
	const HWND front_was = GetForegroundWindow();

	// From here on the desktop gets disturbed, so arm the undo first.
	const Desktop desktop {cursor_was, front_was, &result.restored};

	// Before anything else, because an inactive window eats the click that
	// activates it, and because this changes what is on top at the point
	// the occlusion check below looks at.
	if (!bring_to_front(window, result.activated, error)) {
		busy = true;
		return false;
	}

	// The window moves while it is coming forward on some setups, so the
	// point is mapped again now that it has settled.
	if (!map_point(window, frame, result.screen, error)) return false;

	// Capture sees through whatever is covering the game, so a pattern can
	// be found at a point that on screen belongs to somebody else. Clicking
	// it would press a button in that application instead.
	const POINT at {result.screen.x, result.screen.y};
	const HWND  owner = GetAncestor(WindowFromPoint(at), GA_ROOT);
	if (owner != window) {
		error = (owner ? window_name(owner) : std::string("another window"))
		      + " is on top of the game at the point to be clicked";
		busy = true;
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
		note_our_input();
		return false;
	}

	note_our_input();
	return true;
}

} // namespace

bool click_at(
	HWND                 window,
	const cv::Point&     frame,
	int                  wait_ms,
	const InputPriority& priority,
	const InputStop&     stopping,
	ClickResult&         result,
	std::string&         error)
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
	// Only to settle now that the window can be located at all; try_click
	// maps it again once the game has come forward.
	if (!map_point(window, frame, result.screen, error)) return false;

	const Clock::time_point deadline =
		Clock::now() + priority.USER_PRIORITY_TIMEOUT;

	while (true) {
		if (stopping && stopping()) {
			error = "stopped before the click was made";
			return false;
		}
		if (!wait_for_quiet(
				priority.USER_PRIORITY_IDLE, deadline, stopping,
				result.yielded))
		{
			error = "stopped while waiting for the desktop to be free";
			return false;
		}

		bool busy = false;
		if (try_click(window, frame, result, busy, error)) break;
		// Anything but the desktop being in use would fail the same way
		// however long we waited, so it is reported as it stands.
		if (!busy) return false;
		// And when it is, the error from the last attempt is the right
		// thing to report once the patience runs out.
		if (Clock::now() >= deadline) return false;

		result.yielded = true;
		std::this_thread::sleep_for(YIELD_POLL);
	}

	// Out here rather than inside the attempt, so the game gets its moment
	// to react with the focus already back where it belongs.
	if (wait_ms > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds {wait_ms});
	return true;
}
