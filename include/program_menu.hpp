/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramMenu - the pop-up a hotkey brings up over the game.

	The short way in when the hand is on the mouse and not at the console:
	one key combination, the list of programs over the client, one click or
	one number to start. While a program runs, the same combination shows
	its name and an Abort button instead.

	It lives on its own thread. Only the thread that created a window may
	pump its messages, and the console thread spends its life waiting in
	getline. Everything here is built, drawn and destroyed between start()
	and stop(), and every hook is called from that thread.

	Drawing uses Dear ImGui on a small Direct3D 11 swapchain of its own, and
	on purpose not the one screen capture uses. Sharing a device would make
	capture wait for the menu's lock for as long as it takes to draw a frame.
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

// What the menu shows, and what a click on it does. Every one of these is
// called on the menu's own thread, so what they touch must be ready for
// another thread.
struct MenuHooks {
	// Names of the programs to offer, in the order to list them. Not
	// always every program: the menu shows what it is given and knows
	// nothing about the rest.
	std::function<std::vector<std::string>()> programs;
	// Name of the program in flight, empty when none is.
	std::function<std::string()>              running;
	// Start the program at that position in the programs() list.
	std::function<void(size_t)>               start;
	std::function<void()>                     abort;
	// Window to appear over. May return nullptr, and then the menu uses
	// the monitor the mouse is on.
	std::function<HWND()>                     anchor;
};

class ProgramMenu {
public:
	// The size of the pop-up at 96 dpi. Everything is scaled up from here.
	// The height is not: it is measured from what was drawn.
	static constexpr int WIDTH      = 200;
	static constexpr int ROW_HEIGHT = 28;

	ProgramMenu() = default;
	~ProgramMenu();
	ProgramMenu(const ProgramMenu&)            = delete;
	ProgramMenu& operator=(const ProgramMenu&) = delete;

	// Takes the hotkey and starts the menu thread. Returns false and an
	// explanation when another application already holds the key
	// combination, or when the window or the device could not be created.
	// The console works well without a menu, so that is a warning and not
	// a failure.
	bool start(const Hotkey& key, MenuHooks hooks, std::string& error);
	// Closes the menu and waits for its thread. Safe to call twice, and on
	// a menu that never started.
	void stop();

	bool          running() const noexcept { return m_running.load(); }
	const Hotkey& hotkey() const noexcept { return m_hotkey; }

private:
	static constexpr const wchar_t* WINDOW_CLASS = L"EveBotsProgramMenu";
	static constexpr const wchar_t* WINDOW_TITLE = L"EVE bots";
	static constexpr int            HOTKEY_ID    = 1;

	// How many entries get a number of their own. There are only ten digits,
	// and a longer menu is one to scroll, not one to type at.
	static constexpr size_t NUMBERED = 10;

	// Dark enough to read as an overlay against a lit game window.
	static constexpr float BACKGROUND[4] {0.07f, 0.08f, 0.10f, 1.0f};

	static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
	LRESULT handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

	// Everything the thread does, from setup to shutdown. ready carries
	// the result of the setup back to start(): empty means it worked.
	void thread_main(std::promise<std::string> ready);
	bool build(std::string& error);
	void demolish();

	bool make_render_target();
	void drop_render_target();

	void reveal();    // the hotkey arrived and nothing was showing
	void conceal();   // anything at all that ends the menu
	// One frame. It sizes the window to what it drew, and shows the
	// window only after the size stops changing, so nobody sees it
	// resize itself.
	void draw();

	// Puts a window of this size in the middle of the anchor window, or of
	// the monitor the mouse is on when there is no anchor. It is always
	// fully on screen.
	void place(int width, int height);
	// Builds the style again at this scale. It always starts from the
	// default style instead of scaling the current one, so repeated calls
	// cannot multiply the scale.
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
