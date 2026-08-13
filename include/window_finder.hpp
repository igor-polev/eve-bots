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
// file; matching is case insensitive, an empty field is simply skipped,
// and whatever is left has to hold all at once - neither test alone is
// enough to tell a client from everything else on the desktop.
struct EveWindowMatch {
	std::wstring class_name;   // window class: the launcher shares it
	std::wstring title_prefix; // title prefix: any window may claim it
};

// All visible, non-cloaked top-level windows that have a title.
std::vector<WindowInfo> find_all_windows();

// Subset of the above that matches the given criteria.
std::vector<WindowInfo> find_eve_windows(const EveWindowMatch& match);

// True if the window matches the given criteria.
bool is_eve_window(const WindowInfo& info, const EveWindowMatch& match);

// Who is logged in, taken from the window title: EVE puts the character
// name after the configured prefix, as in "EVE - Jane Doe". A title that
// does not start with the prefix is returned whole - it still tells one
// client from another, which is all this is used for.
std::wstring eve_character_name(
	const std::wstring& title, const std::wstring& prefix
);
