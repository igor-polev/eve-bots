/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramMenu implementation.
*/

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <utility>

#include <dwmapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "program_menu.hpp"

// The Win32 backend passes keyboard and mouse messages to ImGui. It is
// declared in the backend's own source file, not in its header.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND window, UINT message, WPARAM wparam, LPARAM lparam
);

namespace {

constexpr size_t NOT_CHOSEN = static_cast<size_t>(-1);

// The key that starts entry i: 1 for the first, 0 for the tenth, as the
// entries are labelled.
ImGuiKey number_key(size_t entry)
{
	return 9 == entry
		? ImGuiKey_0
		: static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(entry));
}

std::string last_error_text(const char* call)
{
	return std::string(call) + " failed, error "
	     + std::to_string(GetLastError());
}

std::string hresult_text(const char* call, HRESULT result)
{
	return std::string(call) + " failed, 0x"
	     + std::to_string(static_cast<unsigned>(result));
}

// The work area of the monitor a point belongs to.
RECT work_area_at(const POINT& at)
{
	MONITORINFO screen {};
	screen.cbSize = sizeof(screen);
	if (GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &screen))
		return screen.rcWork;

	// No monitor answered, which should not happen. The size of the
	// primary display is at least somewhere on the desktop.
	return RECT {
		0, 0,
		GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)
	};
}

} // namespace

ProgramMenu::~ProgramMenu()
{
	stop();
}

bool ProgramMenu::start(const Hotkey& key, MenuHooks hooks, std::string& error)
{
	if (m_running.load()) {
		error = "the menu is already running";
		return false;
	}
	if (!key.valid()) {
		error = "no hotkey is configured";
		return false;
	}

	m_hotkey = key;
	m_hooks  = std::move(hooks);

	// The thread owns everything it creates, so start() cannot know whether
	// the window and the device were made until the thread says so.
	std::promise<std::string> ready;
	std::future<std::string>  answer = ready.get_future();
	try {
		m_thread = std::thread {
			[this, waiting = std::move(ready)]() mutable {
				thread_main(std::move(waiting));
			}
		};
	}
	catch (const std::exception& e) {
		error = std::string("cannot start the menu thread: ") + e.what();
		return false;
	}

	error = answer.get();
	if (error.empty()) return true;

	m_thread.join();
	return false;
}

void ProgramMenu::stop()
{
	if (m_thread.joinable()) {
		// Wakes the thread wherever it waits. It destroys its own window
		// and device, because only it is allowed to.
		if (m_window) PostMessageW(m_window, WM_CLOSE, 0, 0);
		m_thread.join();
	}
	m_window = nullptr;
}

void ProgramMenu::thread_main(std::promise<std::string> ready)
{
	// Direct3D and DXGI need an apartment, and init_apartment works per
	// thread. The one main() made does nothing for this thread.
	winrt::init_apartment(winrt::apartment_type::multi_threaded);

	std::string error;
	if (!build(error)) {
		demolish();
		winrt::uninit_apartment();
		ready.set_value(error);
		return;
	}
	m_running.store(true);
	ready.set_value(std::string {});

	MSG message {};
	bool quit {false};
	while (!quit) {
		// Nothing to draw while the menu is closed, so the thread costs
		// nothing until a message arrives, usually the hotkey.
		if (!m_visible) WaitMessage();

		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (WM_QUIT == message.message) { quit = true; break; }
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (!quit && m_visible) draw();
	}

	m_running.store(false);
	demolish();
	winrt::uninit_apartment();
}

bool ProgramMenu::build(std::string& error)
{
	const HINSTANCE instance = GetModuleHandleW(nullptr);

	// There is only one menu, and only this thread registers anything, so a
	// plain flag is enough to stop a second start() from failing here.
	static bool registered {false};
	if (!registered) {
		WNDCLASSEXW description {};
		description.cbSize        = sizeof(description);
		description.style         = CS_HREDRAW | CS_VREDRAW;
		description.lpfnWndProc   = window_proc;
		description.hInstance     = instance;
		description.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
		description.lpszClassName = WINDOW_CLASS;
		if (!RegisterClassExW(&description)) {
			error = last_error_text("RegisterClassExW");
			return false;
		}
		registered = true;
	}

	// A tool window, so it stays out of the taskbar and the alt-tab list.
	// Topmost, so it is not drawn behind the game it appeared over. Created
	// hidden, because the first frame decides its size.
	m_window = CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		WINDOW_CLASS, WINDOW_TITLE,
		WS_POPUP,
		0, 0, WIDTH, ROW_HEIGHT,
		nullptr, nullptr, instance, this
	);
	if (!m_window) {
		error = last_error_text("CreateWindowExW");
		return false;
	}

	if (!RegisterHotKey(
			m_window, HOTKEY_ID, m_hotkey.modifiers | MOD_NOREPEAT,
			m_hotkey.key))
	{
		error = hotkey_text(m_hotkey) + " cannot be registered ("
		      + last_error_text("RegisterHotKey")
		      + "); another application most likely has it already";
		return false;
	}
	m_hotkey_held = true;

	DXGI_SWAP_CHAIN_DESC chain {};
	chain.BufferCount        = 2;
	chain.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
	chain.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	chain.OutputWindow       = m_window;
	chain.SampleDesc.Count   = 1;
	chain.Windowed           = TRUE;
	chain.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL wanted[] {
		D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL got {};
	const HRESULT made = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
		&chain, m_swapchain.put(), m_device.put(), &got, m_context.put()
	);
	if (FAILED(made)) {
		error = hresult_text("D3D11CreateDeviceAndSwapChain", made);
		return false;
	}
	if (!make_render_target()) {
		error = "cannot make a render target for the menu window";
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// The menu keeps nothing on disk next to the executable.
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	apply_style(scale());

	if (!ImGui_ImplWin32_Init(m_window)) {
		error = "ImGui_ImplWin32_Init failed";
		return false;
	}
	if (!ImGui_ImplDX11_Init(m_device.get(), m_context.get())) {
		error = "ImGui_ImplDX11_Init failed";
		return false;
	}
	m_imgui_ready = true;
	return true;
}

void ProgramMenu::demolish()
{
	// Backwards through build(), and every step is checked, because
	// demolish() also cleans up after a build() that failed half way.
	if (m_imgui_ready) {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		m_imgui_ready = false;
	}
	if (ImGui::GetCurrentContext()) ImGui::DestroyContext();

	drop_render_target();
	m_swapchain = nullptr;
	m_context   = nullptr;
	m_device    = nullptr;

	if (m_window) {
		if (m_hotkey_held) {
			UnregisterHotKey(m_window, HOTKEY_ID);
			m_hotkey_held = false;
		}
		DestroyWindow(m_window);
		m_window = nullptr;
	}
}

bool ProgramMenu::make_render_target()
{
	winrt::com_ptr<ID3D11Texture2D> back;
	if (FAILED(m_swapchain->GetBuffer(
			0, __uuidof(ID3D11Texture2D), back.put_void())))
	{
		return false;
	}
	return SUCCEEDED(
		m_device->CreateRenderTargetView(back.get(), nullptr, m_target.put())
	);
}

void ProgramMenu::drop_render_target()
{
	m_target = nullptr;
}

LRESULT CALLBACK ProgramMenu::window_proc(
	HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	ProgramMenu* menu {nullptr};
	if (WM_NCCREATE == message) {
		const CREATESTRUCTW* created =
			reinterpret_cast<CREATESTRUCTW*>(lparam);
		menu = static_cast<ProgramMenu*>(created->lpCreateParams);
		SetWindowLongPtrW(
			window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu)
		);
	} else {
		menu = reinterpret_cast<ProgramMenu*>(
			GetWindowLongPtrW(window, GWLP_USERDATA)
		);
	}
	return menu
		? menu->handle(window, message, wparam, lparam)
		: DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ProgramMenu::handle(
	HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	// ImGui must see the input before anything else, and it says that it
	// used a message by claiming it.
	if (m_imgui_ready
		&& ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
	{
		return 1;
	}

	switch (message) {
	case WM_HOTKEY:
		// The same combination closes it again, so a menu opened by
		// mistake costs one keystroke and not a reach for the mouse.
		if (HOTKEY_ID == static_cast<int>(wparam)) {
			if (m_visible) conceal(); else reveal();
		}
		return 0;

	case WM_SIZE:
		if (m_device && SIZE_MINIMIZED != wparam) {
			drop_render_target();
			m_swapchain->ResizeBuffers(
				0, LOWORD(lparam), HIWORD(lparam), DXGI_FORMAT_UNKNOWN, 0
			);
			make_render_target();
		}
		return 0;

	case WM_ACTIVATE:
		// A click anywhere else means the menu was not wanted. This counts
		// only once the menu is really on screen: the frames that measure
		// it are drawn hidden, and those must not close it.
		if (WA_INACTIVE == LOWORD(wparam) && m_shown) conceal();
		return 0;

	case WM_CLOSE:
		// Asked to shut down. demolish() destroys the window itself after
		// the loop ends, on this same thread.
		PostQuitMessage(0);
		return 0;

	case WM_DPICHANGED:
		// Dragged onto another display, or the display was rescaled. The
		// next frame sizes and places the window again.
		m_dpi = HIWORD(wparam);
		apply_style(scale());
		m_height = 0;
		return 0;

	default:
		break;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

void ProgramMenu::reveal()
{
	if (m_visible) return;
	m_visible = true;
	m_shown   = false;
	m_height  = 0;   // unknown until something has been drawn

	// Place it where it will appear before asking how big anything is, so
	// the dpi used is the one of the monitor it lands on.
	place(
		static_cast<int>(WIDTH * scale() + 0.5f),
		static_cast<int>(ROW_HEIGHT * scale() + 0.5f)
	);
	const UINT dpi = GetDpiForWindow(m_window);
	if (dpi > 0 && dpi != m_dpi) {
		m_dpi = dpi;
		apply_style(scale());
	}
}

void ProgramMenu::conceal()
{
	m_visible = false;
	m_shown   = false;
	ShowWindow(m_window, SW_HIDE);
}

void ProgramMenu::place(int width, int height)
{
	RECT over {};
	bool anchored {false};

	const HWND anchor = m_hooks.anchor ? m_hooks.anchor() : nullptr;
	if (anchor && IsWindow(anchor) && !IsIconic(anchor)) {
		// The frame bounds, not the window rectangle. That is the same
		// measurement clicks are aimed with, so the menu sits exactly where
		// it looks like it sits.
		anchored = SUCCEEDED(DwmGetWindowAttribute(
			anchor, DWMWA_EXTENDED_FRAME_BOUNDS, &over, sizeof(over)
		));
	}
	if (!anchored) {
		// Nothing to appear over. Somebody who just pressed a hotkey is
		// looking at the monitor the mouse is on.
		POINT cursor {};
		GetCursorPos(&cursor);
		over = work_area_at(cursor);
	}

	int left = over.left + (over.right  - over.left - width)  / 2;
	int top  = over.top  + (over.bottom - over.top  - height) / 2;

	// A game window can hang over the edge of the desktop, or be larger than
	// the display it is on. The menu must not follow it out there.
	const POINT middle {left + width / 2, top + height / 2};
	const RECT  screen = work_area_at(middle);
	left = std::clamp(left, static_cast<int>(screen.left),
	                  std::max(static_cast<int>(screen.left),
	                           static_cast<int>(screen.right) - width));
	top  = std::clamp(top, static_cast<int>(screen.top),
	                  std::max(static_cast<int>(screen.top),
	                           static_cast<int>(screen.bottom) - height));

	SetWindowPos(
		m_window, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE
	);
}

void ProgramMenu::apply_style(float scale)
{
	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle {};   // the defaults, so rescaling cannot compound

	style.WindowRounding   = 6.0f;
	style.WindowBorderSize = 1.0f;
	style.WindowPadding    = ImVec2 {10.0f, 10.0f};
	style.FrameRounding    = 4.0f;
	style.ItemSpacing      = ImVec2 {6.0f, 6.0f};

	style.ScaleAllSizes(scale);
	// Since 1.92 sizes and fonts scale separately, and both need the dpi.
	style.FontScaleMain = scale;
}

void ProgramMenu::draw()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2 {0.0f, 0.0f});
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin(
		"##eve_bots_menu", nullptr,
		ImGuiWindowFlags_NoTitleBar     | ImGuiWindowFlags_NoResize      |
		ImGuiWindowFlags_NoMove         | ImGuiWindowFlags_NoCollapse    |
		ImGuiWindowFlags_NoScrollbar    | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings
	);

	const ImVec2 row {-FLT_MIN, ROW_HEIGHT * scale()};
	size_t chosen     {NOT_CHOSEN};
	bool   stop_asked {false};

	// The runner allows only one program at a time, so while one runs there
	// is nothing to choose between: the only question is whether to stop it.
	const std::string busy =
		m_hooks.running ? m_hooks.running() : std::string {};
	if (busy.empty()) {
		ImGui::TextDisabled("EVE BOTS");
		ImGui::Separator();
		const std::vector<std::string> names =
			m_hooks.programs ? m_hooks.programs() : std::vector<std::string> {};
		if (names.empty()) {
			ImGui::TextDisabled("no programs");
		} else {
			for (size_t i = 0; i < names.size(); ++i) {
				// Numbered 1 to 9 and then 0, the way a person counts a
				// short list, so the key to press is on the entry itself.
				const std::string label = i < NUMBERED
					? std::to_string((i + 1) % NUMBERED) + "   " + names[i]
					: "    " + names[i];
				if (ImGui::Button(label.c_str(), row)) chosen = i;
				if (i < NUMBERED && ImGui::IsKeyPressed(number_key(i)))
					chosen = i;
			}
		}
	} else {
		ImGui::TextDisabled("RUNNING");
		ImGui::Separator();
		ImGui::TextUnformatted(busy.c_str());
		if (ImGui::Button("Abort", row)) stop_asked = true;
	}

	// Where the content ended, which is how tall the window wants to be.
	// Read before End(), while this is still the current window.
	const float wanted =
		ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
	const bool dismissed = ImGui::IsKeyPressed(ImGuiKey_Escape);

	ImGui::End();
	ImGui::Render();

	ID3D11RenderTargetView* view = m_target.get();
	if (view) {
		m_context->OMSetRenderTargets(1, &view, nullptr);
		m_context->ClearRenderTargetView(view, BACKGROUND);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	// Paced by the display, which is all the throttling a menu that is up
	// for a second or two at a time needs.
	m_swapchain->Present(1, 0);

	// Whatever was clicked, the menu has done its job and gets out of the
	// way first: the program it starts will be reaching for the same
	// screen this window is sitting on.
	if (NOT_CHOSEN != chosen) {
		conceal();
		if (m_hooks.start) m_hooks.start(chosen);
		return;
	}
	if (stop_asked) {
		conceal();
		if (m_hooks.abort) m_hooks.abort();
		return;
	}
	if (dismissed) {
		conceal();
		return;
	}

	// The window is sized to fit what was just drawn, and is shown only
	// after it fits. A pop-up seen resizing itself looks broken.
	const int height = static_cast<int>(wanted + 0.5f);
	if (height > 0 && height != m_height) {
		m_height = height;
		place(static_cast<int>(WIDTH * scale() + 0.5f), height);
		return;
	}
	if (!m_shown) {
		m_shown = true;
		ShowWindow(m_window, SW_SHOW);
		// Windows grants the foreground to a process that has just been
		// sent a hotkey, which is the only reason this is allowed to work.
		SetForegroundWindow(m_window);
	}
}
