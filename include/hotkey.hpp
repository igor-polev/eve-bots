/*
	EVE bots for Windows.
	Author: Igor Polev.

	Hotkey - a modifier and key combination, as written in eve_config.json.

	Written the way a person would write it: "Alt+`", "Ctrl+Shift+Space",
	"Alt+F1". Keys are named by their place on the keyboard, not by the
	character they print, because that is what the hand knows: the key left
	of '1' is "`" here, whatever the layout prints on it.
*/

#pragma once

#include <string>

#include <windows.h>

struct Hotkey {
	UINT modifiers {0};   // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN
	UINT key       {0};   // virtual key code

	// A combination with no key is not a combination. That is how an empty
	// setting says the menu should not open at all.
	bool valid() const noexcept { return 0 != key; }
};

// "Alt+`" -> {MOD_ALT, VK_OEM_3}. Returns false and an explanation for
// anything it cannot use. An empty string is not an error: it gives an
// invalid hotkey, and that turns the menu off.
bool parse_hotkey(const std::string& text, Hotkey& key, std::string& error);

// The same combination written back, for messages: "Alt+`". An invalid
// hotkey comes back as "none".
std::string hotkey_text(const Hotkey& key);
