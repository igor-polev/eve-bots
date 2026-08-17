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
// file. Matching ignores case, an empty field is skipped, and every field
// that is left must match at the same time. One test alone cannot tell a
// client from everything else on the desktop.
struct EveWindowMatch {
	std::wstring class_name;   // window class: the launcher shares it
	std::wstring title_prefix; // title prefix: any window may claim it
};

// All visible, non-cloaked top level windows that have a title.
std::vector<WindowInfo> find_all_windows();

// The windows from the list above that match the given rules.
std::vector<WindowInfo> find_eve_windows(const EveWindowMatch& match);

// True when the window matches the given rules.
bool is_eve_window(const WindowInfo& info, const EveWindowMatch& match);

// Who is logged in, taken from the window title. EVE puts the character
// name after the prefix from the config file, as in "EVE - Jane Doe". A
// title that does not start with the prefix is returned whole: it still
// tells one client from another, and that is all this is used for.
std::wstring eve_character_name(
	const std::wstring& title, const std::wstring& prefix
);
