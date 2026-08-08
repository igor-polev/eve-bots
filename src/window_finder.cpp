/*
	EVE bots for Windows.
	Author: Igor Polev.

	Window enumeration implementation.
*/

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <dwmapi.h>

#include "window_finder.hpp"

namespace {

std::wstring to_lower(std::wstring text)
{
	std::transform(
		text.begin(), text.end(), text.begin(),
		[](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); }
	);
	return text;
}

bool starts_with(const std::wstring& text, const std::wstring& prefix)
{
	return text.size() >= prefix.size()
		&& 0 == text.compare(0, prefix.size(), prefix);
}

// UWP/shell windows stay in the enumeration as invisible "ghosts".
bool is_cloaked(HWND hwnd)
{
	int cloaked {0};
	HRESULT hr = DwmGetWindowAttribute(
		hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)
	);
	return SUCCEEDED(hr) && cloaked != 0;
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM param)
{
	auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(param);

	if (!IsWindowVisible(hwnd))  return TRUE;
	if (is_cloaked(hwnd))        return TRUE;

	// skip tool windows - palettes, tooltips and the like
	if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
		return TRUE;

	int title_len = GetWindowTextLengthW(hwnd);
	if (title_len <= 0) return TRUE;

	WindowInfo info;
	info.hwnd = hwnd;
	info.title.resize(static_cast<size_t>(title_len) + 1);
	int copied = GetWindowTextW(
		hwnd, info.title.data(), static_cast<int>(info.title.size())
	);
	info.title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
	if (info.title.empty()) return TRUE;

	wchar_t class_buffer[256] {};
	int class_len = GetClassNameW(
		hwnd, class_buffer, static_cast<int>(std::size(class_buffer))
	);
	info.class_name.assign(
		class_buffer, class_len > 0 ? static_cast<size_t>(class_len) : 0
	);

	GetWindowThreadProcessId(hwnd, &info.pid);
	windows->push_back(std::move(info));
	return TRUE;
}

} // namespace

bool is_eve_window(const WindowInfo& info, const EveWindowMatch& match)
{
	// Every criterion that was given has to hold. Either one on its own
	// catches something else: the class is shared with the launcher, and
	// the title is shared with anything that happens to be showing the
	// word EVE - a browser reading about the game, most easily.
	if (!match.class_name.empty()
		&& to_lower(info.class_name) != to_lower(match.class_name))
		return false;
	if (!match.title_prefix.empty()
		&& !starts_with(to_lower(info.title), to_lower(match.title_prefix)))
		return false;
	return true;
}

std::wstring eve_character_name(
	const std::wstring& title, const std::wstring& prefix)
{
	std::wstring name {title};
	// The same case insensitive test is_eve_window() matches the prefix by,
	// so a title accepted there is stripped here.
	if (!prefix.empty() && starts_with(to_lower(name), to_lower(prefix)))
		name.erase(0, prefix.size());

	const size_t first = name.find_first_not_of(L" \t");
	const size_t last  = name.find_last_not_of(L" \t");
	if (std::wstring::npos == first) return title;   // prefix and nothing else
	return name.substr(first, last - first + 1);
}

std::vector<WindowInfo> find_all_windows()
{
	std::vector<WindowInfo> windows;
	EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&windows));
	return windows;
}

std::vector<WindowInfo> find_eve_windows(const EveWindowMatch& match)
{
	std::vector<WindowInfo> windows = find_all_windows();
	std::vector<WindowInfo> eve_windows;
	for (auto& info : windows)
		if (is_eve_window(info, match))
			eve_windows.push_back(std::move(info));
	return eve_windows;
}
