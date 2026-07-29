/*
	EVE bots for Windows.
	Author: Igor Polev.

	Top-level window enumeration and EVE Online client detection.
*/

#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct WindowInfo {
	HWND         hwnd {nullptr};
	std::wstring title;
	std::wstring class_name;
	DWORD        pid {0};
};

// How to recognise an EVE client window. Both fields come from the config
// file; matching is case insensitive and an empty field is simply skipped.
struct EveWindowMatch {
	std::wstring class_name;   // window class, the definitive test
	std::wstring title_prefix; // title prefix, fallback if the class changes
};

// All visible, non-cloaked top-level windows that have a title.
std::vector<WindowInfo> find_all_windows();

// Subset of the above that matches the given criteria.
std::vector<WindowInfo> find_eve_windows(const EveWindowMatch& match);

// True if the window matches the given criteria.
bool is_eve_window(const WindowInfo& info, const EveWindowMatch& match);
