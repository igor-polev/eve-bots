/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramMenu - the pop-up a hotkey brings up over the game.

	The console is the whole interface, but a hand on the mouse in the
	middle of something is nowhere near it. This is the short way in: one
	key combination, the list of programs over the centre of the client,
	one click to start, and it is gone again. While a program is running
	the same combination brings up its name and an Abort button instead,
	since starting a second one is not something the runner allows.

	It lives on its own thread. A window belongs to the thread that
	created it and only that thread may pump its messages, and the
	console thread spends its life blocked in getline - so everything
	here is built, drawn and torn down between start() and stop(), and
	the hooks it was handed are all called from that thread.

	Drawing is Dear ImGui on a small Direct3D 11 swapchain of its own,
	deliberately not the one screen capture uses: sharing a device would
	put the capture path behind the menu's lock for as long as a frame
	takes to draw. Nothing is drawn while the menu is down - the thread
	sleeps in WaitMessage until the hotkey arrives.
*/

#pragma once
#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <d3d11.h>
#include <winrt/base.h>

#include "hotkey.hpp"

// What the menu shows, and what clicking it does. Every one of these is
// called on the menu's own thread, so whatever they touch has to be
// ready for that.
struct MenuHooks {
	// Names of the programs, in the order they should be listed.
	std::function<std::vector<std::string>()> programs;
	// Name of the program in flight, empty when none is.
	std::function<std::string()>              running;
	// Start the program at that position in the programs() list.
	std::function<void(size_t)>               start;
	std::function<void()>                     abort;
	// Window to appear over. May return nullptr - the menu then falls
	// back to the monitor the mouse is on.
	std::function<HWND()>                     anchor;
};

class ProgramMenu {
public:
	// The pop-up's shape at 96 dpi; everything is scaled up from here.
	// The height is not among them: it is measured from what was drawn.
	static constexpr int WIDTH      = 200;
	static constexpr int ROW_HEIGHT = 28;

	ProgramMenu() = default;
	~ProgramMenu();
	ProgramMenu(const ProgramMenu&)            = delete;
	ProgramMenu& operator=(const ProgramMenu&) = delete;

	// Claims the hotkey and starts the menu thread. False and an
	// explanation when the combination is already spoken for, or when the
	// window or the device could not be had; the console works perfectly
	// well without a menu, so that is a warning rather than a failure.
	bool start(const Hotkey& key, MenuHooks hooks, std::string& error);
	// Closes the menu and waits for its thread. Safe to call twice, and
	// on a menu that never started.
	void stop();

	bool          running() const noexcept { return m_running.load(); }
	const Hotkey& hotkey() const noexcept { return m_hotkey; }

private:
	static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
	LRESULT handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

	// Everything the thread does, from setup to teardown. ready carries
	// the outcome of the setup back to start(): empty means it worked.
	void thread_main(std::promise<std::string> ready);
	bool build(std::string& error);
	void demolish();

	bool make_render_target();
	void drop_render_target();

	void reveal();    // the hotkey arrived and nothing was showing
	void conceal();   // anything at all that ends the menu
	// One frame. Sizes the window to what it drew and only puts it on
	// screen once that has settled, so it cannot be seen resizing itself.
	void draw();

	// Centres a window of this size over the anchor, or over the monitor
	// the mouse is on when there is none, wholly on screen either way.
	void place(int width, int height);
	// Rebuilds the style at this scale. Built from the defaults every
	// time rather than scaled again, so repeated calls cannot compound.
	void apply_style(float scale);
	float scale() const noexcept { return m_dpi / 96.0f; }

	Hotkey    m_hotkey;
	MenuHooks m_hooks;

	std::atomic<bool> m_running {false};
	std::thread       m_thread;

	HWND m_window {nullptr};
	bool m_hotkey_held {false};   // RegisterHotKey succeeded, so unregister
	bool m_imgui_ready {false};   // safe to hand messages to the backend

	winrt::com_ptr<ID3D11Device>           m_device;
	winrt::com_ptr<ID3D11DeviceContext>    m_context;
	winrt::com_ptr<IDXGISwapChain>         m_swapchain;
	winrt::com_ptr<ID3D11RenderTargetView> m_target;

	bool m_visible {false};   // the menu is up, or on its way up
	bool m_shown   {false};   // the window itself is on screen
	int  m_height  {0};       // client height the last frame asked for
	UINT m_dpi     {96};      // of the monitor it last appeared on
};
