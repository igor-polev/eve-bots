/*
	EVE bots for Windows.
	Author: Igor Polev.

	Hotkey implementation.
*/

#include <algorithm>
#include <cctype>
#include <vector>

#include "hotkey.hpp"

namespace {

struct Named { const char* name; UINT value; };

const Named MODIFIER_NAMES[] = {
	{"ctrl",  MOD_CONTROL}, {"control", MOD_CONTROL},
	{"alt",   MOD_ALT},
	{"shift", MOD_SHIFT},
	{"win",   MOD_WIN},     {"windows", MOD_WIN},
};

// Modifiers in the order hotkey_text spells them, which is the order
// people write them in.
const Named MODIFIER_ORDER[] = {
	{"Ctrl", MOD_CONTROL}, {"Alt", MOD_ALT},
	{"Shift", MOD_SHIFT},  {"Win", MOD_WIN},
};

// The first spelling of each key is the one hotkey_text hands back; the rest
// are alternatives it accepts. The punctuation keys are OEM codes, which name
// a position on the keyboard rather than a character.
const Named KEY_NAMES[] = {
	{"`", VK_OEM_3}, {"backquote", VK_OEM_3}, {"grave", VK_OEM_3},
	{"tilde", VK_OEM_3},
	{"-", VK_OEM_MINUS},  {"minus", VK_OEM_MINUS},
	{"=", VK_OEM_PLUS},   {"equals", VK_OEM_PLUS},
	{"[", VK_OEM_4},      {"]", VK_OEM_6},        {"\\", VK_OEM_5},
	{";", VK_OEM_1},      {"'", VK_OEM_7},
	{",", VK_OEM_COMMA},  {".", VK_OEM_PERIOD},   {"/", VK_OEM_2},

	{"space", VK_SPACE},      {"tab", VK_TAB},
	{"enter", VK_RETURN},     {"return", VK_RETURN},
	{"esc", VK_ESCAPE},       {"escape", VK_ESCAPE},
	{"backspace", VK_BACK},
	{"insert", VK_INSERT},    {"delete", VK_DELETE},  {"del", VK_DELETE},
	{"home", VK_HOME},        {"end", VK_END},
	{"pageup", VK_PRIOR},     {"pagedown", VK_NEXT},
	{"left", VK_LEFT},        {"right", VK_RIGHT},
	{"up", VK_UP},            {"down", VK_DOWN},
	{"scrolllock", VK_SCROLL},{"scroll", VK_SCROLL},
	{"pause", VK_PAUSE},      {"numlock", VK_NUMLOCK},
	{"apps", VK_APPS},        {"menu", VK_APPS},
};

std::string lowered(std::string text)
{
	std::transform(
		text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(::tolower(c)); }
	);
	return text;
}

std::string trimmed(const std::string& text)
{
	const size_t first = text.find_first_not_of(" \t");
	if (std::string::npos == first) return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// "Ctrl + Alt + E" -> the three pieces, spaces gone. Empty pieces are
// kept, so "Ctrl+" is caught as a hotkey with no key rather than read as
// a bare "Ctrl".
std::vector<std::string> split_plus(const std::string& text)
{
	std::vector<std::string> pieces;
	size_t start {0};
	while (true) {
		const size_t plus = text.find('+', start);
		if (std::string::npos == plus) {
			pieces.push_back(trimmed(text.substr(start)));
			return pieces;
		}
		pieces.push_back(trimmed(text.substr(start, plus - start)));
		start = plus + 1;
	}
}

// Whether a lone press of this key would otherwise have typed something.
// Those need a modifier: a hotkey is registered system wide, so claiming
// bare 'E' would take the letter away from every application there is.
bool prints_something(UINT key)
{
	if (key >= 'A' && key <= 'Z') return true;
	if (key >= '0' && key <= '9') return true;
	if (key >= VK_NUMPAD0 && key <= VK_DIVIDE) return true;
	switch (key) {
	case VK_OEM_1: case VK_OEM_2: case VK_OEM_3: case VK_OEM_4:
	case VK_OEM_5: case VK_OEM_6: case VK_OEM_7:
	case VK_OEM_PLUS: case VK_OEM_MINUS:
	case VK_OEM_COMMA: case VK_OEM_PERIOD:
	case VK_SPACE: case VK_TAB: case VK_RETURN: case VK_BACK:
		return true;
	default:
		return false;
	}
}

// A whole-token unsigned number, for the tail of "f12" and "numpad3".
bool parse_index(const std::string& text, unsigned& value)
{
	if (text.empty()) return false;
	unsigned parsed {0};
	for (const char digit : text) {
		if (digit < '0' || digit > '9') return false;
		parsed = parsed * 10 + static_cast<unsigned>(digit - '0');
		if (parsed > 999) return false;
	}
	value = parsed;
	return true;
}

// The named key, 0 when there is none by that name.
UINT key_code(const std::string& name)
{
	if (1 == name.size()) {
		const unsigned char single = static_cast<unsigned char>(name[0]);
		// Letters and digits are their own virtual key codes, upper case.
		if (std::isalnum(single))
			return static_cast<UINT>(::toupper(single));
	}
	// F1..F24 and numpad0..numpad9 are runs, not worth a table each.
	unsigned index {0};
	if ('f' == name[0] && parse_index(name.substr(1), index)
		&& index >= 1 && index <= 24)
	{
		return VK_F1 + index - 1;
	}
	if (0 == name.compare(0, 6, "numpad") && parse_index(name.substr(6), index)
		&& index <= 9)
	{
		return VK_NUMPAD0 + index;
	}

	for (const Named& known : KEY_NAMES)
		if (name == known.name) return known.value;
	return 0;
}

// The canonical spelling of a key code, empty when it has none.
std::string key_name(UINT key)
{
	if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
		return std::string(1, static_cast<char>(key));
	if (key >= VK_F1 && key <= VK_F24)
		return "F" + std::to_string(key - VK_F1 + 1);
	if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
		return "Numpad" + std::to_string(key - VK_NUMPAD0);

	for (const Named& known : KEY_NAMES)
		if (key == known.value) return known.name;
	return {};
}

} // namespace

bool parse_hotkey(const std::string& text, Hotkey& key, std::string& error)
{
	key = Hotkey {};
	// Not a mistake, and not a hotkey either: the menu is simply off.
	if (trimmed(text).empty()) return true;

	const std::vector<std::string> pieces = split_plus(text);
	Hotkey parsed;
	for (size_t i = 0; i < pieces.size(); ++i) {
		const std::string name = lowered(pieces[i]);
		if (name.empty()) {
			error = "'" + text + "' has an empty piece between its '+' signs";
			return false;
		}

		// Everything before the last piece must be a modifier, and the last
		// piece must be the key - "Alt+Ctrl" names no key at all.
		const bool last = (i + 1 == pieces.size());
		if (!last) {
			UINT modifier {0};
			for (const Named& known : MODIFIER_NAMES)
				if (name == known.name) modifier = known.value;
			if (0 == modifier) {
				error = "'" + pieces[i] + "' is not a modifier; "
				        "Ctrl, Alt, Shift and Win are";
				return false;
			}
			if (parsed.modifiers & modifier) {
				error = "'" + pieces[i] + "' is named twice in '" + text + "'";
				return false;
			}
			parsed.modifiers |= modifier;
			continue;
		}

		parsed.key = key_code(name);
		if (0 == parsed.key) {
			error = "no key called '" + pieces[i] + "'; a letter, a digit, "
			        "F1 to F24, Numpad0 to Numpad9, a punctuation mark such "
			        "as ` or /, or a name such as Space, Tab, Enter, Esc, "
			        "Home, PageUp, Left or ScrollLock";
			return false;
		}
	}

	// A bare printing key would be taken away from every other application
	// on the desktop, which is never what somebody meant to ask for.
	if (0 == parsed.modifiers && prints_something(parsed.key)) {
		error = "'" + text + "' needs a modifier: registering it on its own "
		        "would take the key away from every other application. Try "
		        "Alt+" + text + ".";
		return false;
	}

	key = parsed;
	return true;
}

std::string hotkey_text(const Hotkey& key)
{
	if (!key.valid()) return "none";

	std::string text;
	for (const Named& known : MODIFIER_ORDER)
		if (key.modifiers & known.value) text += std::string(known.name) + "+";

	const std::string named = key_name(key.key);
	return text + (named.empty()
		? "key " + std::to_string(key.key)   // registered, just unnameable
		: named);
}
