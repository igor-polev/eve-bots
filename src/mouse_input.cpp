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

// How long the button stays down. Not politeness: the client was measured
// redrawing at 13-41 fps, and an engine that samples input once per rendered
// frame can miss a press and release that arrive between two samples.
constexpr eb::Millis CLICK_HOLD {40};

// Gap between putting the cursor on the target and pressing the button. Sent
// together they arrive in the same instant and a control that highlights on
// hover never sees the hover.
constexpr eb::Millis MOVE_SETTLE {80};

// Activation is asynchronous, so it has to be waited for rather than
// assumed, and a window that has just come forward needs a moment before it
// is listening again.
constexpr eb::Millis FOCUS_TIMEOUT {1500};
constexpr eb::Millis FOCUS_POLL    {25};
constexpr eb::Millis FOCUS_SETTLE  {150};

// How often the desktop is looked at while waiting for it to be free.
constexpr eb::Millis YIELD_POLL {50};

// GetLastInputInfo counts our own injected input too, so our clicks look
// exactly like a busy user: after one click the desktop would seem to be in
// use for the whole idle time, every time, because of us. So we remember
// the tick of the last event we sent, and anything not newer than that did
// not come from the user.
std::atomic<DWORD> g_our_input  {0};
std::atomic<DWORD> g_user_input {0};
std::atomic<bool>  g_user_known {false};

void note_our_input() { g_our_input.store(GetTickCount()); }

// How long since the user last touched the mouse or the keyboard. The tick
// count wraps around every 49 days, so the tests compare signed differences
// and not the values themselves.
eb::Millis user_quiet_for()
{
	LASTINPUTINFO info {};
	info.cbSize = sizeof(info);
	// It is not known to fail. If it ever does, the answer that keeps
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
	return eb::Millis {std::max<LONG>(0, quiet)};
}

// Waits for a gap in the user's work. Running out of time ends the waiting,
// not the click: holding a program up for ever is worse than one
// interrupted keystroke, so the caller goes ahead.
void wait_for_quiet(eb::Millis needed, eb::TimePoint deadline, bool& yielded)
{
	if (needed <= eb::Millis::zero()) return;

	while (user_quiet_for() < needed) {
		if (eb::Clock::now() >= deadline) return;
		yielded = true;
		std::this_thread::sleep_for(YIELD_POLL);
	}
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

// Name of the window that owns a point on the desktop, for the message
// that explains a refused click.
std::string window_name(HWND window)
{
	wchar_t title[128] {};
	const int length = GetWindowTextW(window, title, 128);
	if (length <= 0) return "another window";

	// The console is UTF-8 and these titles are only shown to a person,
	// so losing some characters here is good enough.
	std::string name;
	for (int i = 0; i < length; ++i) {
		const wchar_t c = title[i];
		name += (c < 32 || c > 126) ? '?' : static_cast<char>(c);
	}
	return "'" + name + "'";
}

// Asks for a window to be brought to the front. Does not wait for it.
//
// Windows lets only two kinds of process hand the foreground to somebody
// else: the one that already holds it, and the one that sent the last
// input. Everybody else gets a flashing taskbar button. Attaching our input
// queue to the thread of the foreground window makes Windows treat the two
// as one thread, and that is the documented way around the rule.
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

// Puts the desktop back as it was: the cursor where it was, and the focus
// on the window that held it. This happens however click_at() ends. It is a
// destructor and not a call at the end, so a click that is refused after
// the game was raised does not keep the focus it took.
class Desktop {
public:
	Desktop(const POINT& cursor, HWND front, bool* restored)
		: m_cursor {cursor}, m_front {front}, m_restored {restored} {}
	Desktop(const Desktop&)            = delete;
	Desktop& operator=(const Desktop&) = delete;

	~Desktop()
	{
		SetCursorPos(m_cursor.x, m_cursor.y);
		// Putting the cursor back is input too, and would otherwise look
		// like the user moving the mouse.
		note_our_input();

		// Nothing to give back when the game already had the focus, or
		// when the window that held it has closed.
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

	for (eb::Millis waited {0};
	     waited < FOCUS_TIMEOUT;
	     waited += FOCUS_POLL)
	{
		if (GetForegroundWindow() == window) {
			// Being in front is not the same as being ready for input. The
			// game starts reading input again on its next frame.
			std::this_thread::sleep_for(FOCUS_SETTLE);
			return true;
		}
		std::this_thread::sleep_for(FOCUS_POLL);
	}

	error = "the window would not come to the front, and a click on an "
	        "inactive window is swallowed to activate it instead of acting";
	return false;
}

// One attempt at the disturbing part: raise the game, check that the point
// on screen still belongs to it, and click. The desktop is put back however
// this ends. busy means the desktop was in somebody else's hands, and that
// is the one result worth trying again.
bool try_click(
	HWND             window,
	const cv::Point& frame,
	ClickResult&     result,
	bool&            busy,
	std::string&     error)
{
	busy = false;

	// Read before anything is disturbed, so it can all be put back.
	POINT cursor_was {};
	GetCursorPos(&cursor_was);
	const HWND front_was = GetForegroundWindow();

	// From here on the desktop is disturbed, so set up the undo first.
	const Desktop desktop {cursor_was, front_was, &result.restored};

	// This comes first, because an inactive window eats the click that
	// activates it, and because raising the window changes what is on top
	// at the point the check below looks at.
	if (!bring_to_front(window, result.activated, error)) {
		busy = true;
		return false;
	}

	// On some systems the window moves while it comes to the front, so the
	// point is mapped again now that the window has settled.
	if (!map_point(window, frame, result.screen, error)) return false;

	// Capture sees through anything that covers the game, so a pattern can be
	// found at a point that belongs on screen to another window. Clicking it
	// would press a button in that other application.
	const POINT at {result.screen.x, result.screen.y};
	const HWND  owner = GetAncestor(WindowFromPoint(at), GA_ROOT);
	if (owner != window) {
		error = (owner ? window_name(owner) : std::string("another window"))
		      + " is on top of the game at the point to be clicked";
		busy = true;
		return false;
	}

	// Absolute SendInput coordinates run 0..65535 across the whole virtual
	// desktop. They are not pixels, and not relative to one monitor.
	const int left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (width < 2 || height < 2) {
		error = "cannot size the virtual desktop";
		return false;
	}

	// Move, press and release are sent one at a time with a pause between,
	// so each lands in a different rendered frame. Sent together they
	// arrive in the same instant, and the game can miss the pointer being
	// there at all.
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
		// the button is down and stays down, so say it plainly
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
	eb::Millis           wait,
	const InputPriority& priority,
	ClickResult&         result,
	std::string&         error)
{
	if (!window || !IsWindow(window)) {
		error = "the captured window is gone";
		return false;
	}
	// Nothing useful can be clicked on a window that is not on screen, and
	// capture would have stopped sending frames anyway.
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
	// Only to check that the window can be found at all. try_click maps the
	// point again once the game is in front.
	if (!map_point(window, frame, result.screen, error)) return false;

	const eb::TimePoint deadline =
		eb::Clock::now() + priority.USER_PRIORITY_TIMEOUT;

	while (true) {
		wait_for_quiet(priority.USER_PRIORITY_IDLE, deadline, result.yielded);

		bool busy = false;
		if (try_click(window, frame, result, busy, error)) break;
		// Anything except a busy desktop would fail the same way however
		// long we waited, so it is reported as it is.
		if (!busy) return false;
		// And when the desktop is busy, the error from the last attempt
		// is the right one to report when the waiting runs out.
		if (eb::Clock::now() >= deadline) return false;

		result.yielded = true;
		std::this_thread::sleep_for(YIELD_POLL);
	}

	// Out here and not inside the attempt, so the game gets its moment to
	// react with the focus already back where it belongs.
	if (wait > eb::Millis::zero()) std::this_thread::sleep_for(wait);
	return true;
}
