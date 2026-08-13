/*
	EVE bots for Windows.
	Author: Igor Polev.

	Hotkey - a modifier and key combination, as written in eve_config.json.

	Spelt the way a person would: "Alt+`", "Ctrl+Shift+Space", "Alt+F1".
	Keys are named by where they sit on the keyboard rather than by what
	they print, because that is what a hand reaching for one knows: the
	key left of '1' is "`" here whatever the layout puts on its face.
*/

#pragma once

#include <string>

#include <windows.h>

struct Hotkey {
	UINT modifiers {0};   // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN
	UINT key       {0};   // virtual key code

	// A combination with no key is not a combination: that is how an empty
	// setting says the menu should not be summonable at all.
	bool valid() const noexcept { return 0 != key; }
};

// "Alt+`" -> {MOD_ALT, VK_OEM_3}. False and an explanation for anything
// unusable. An empty string is not an error: it parses to an invalid
// hotkey, which is what turns the menu off.
bool parse_hotkey(const std::string& text, Hotkey& key, std::string& error);

// The same combination spelt back, for messages: "Alt+`". An invalid
// hotkey comes back as "none".
std::string hotkey_text(const Hotkey& key);
