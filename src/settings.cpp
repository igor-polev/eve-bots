/*
	EVE bots for Windows.
	Author: Igor Polev.

	Settings implementation.
*/

#include <fstream>

#include <windows.h>
#include <nlohmann/json.hpp>

#include "paths.hpp"
#include "settings.hpp"
#include "text_util.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* KEY_WINDOW_CLASS = "EVE_WINDOW_CLASS_NAME";
constexpr const char* KEY_TITLE_PREFIX = "EVE_WINDOW_TITLE_PREFIX";
constexpr const char* KEY_FRAME_RATE   = "CAPTURE_FRAME_RATE_DEFAULT";
constexpr const char* KEY_IMAGE_DIR    = "IMAGE_LIBRARY_DIR";
constexpr const char* KEY_THRESHOLD    = "DETECT_THRESHOLD_DEFAULT";
constexpr const char* KEY_MIN_MARGINE  = "MIN_MARGINE";
constexpr const char* KEY_MENU_HOTKEY  = "MENU_HOTKEY";

// Reads a required string setting.
std::wstring read_string(const json& settings, const char* key)
{
	const json& value = settings.at(key);
	if (!value.is_string())
		throw std::runtime_error(std::string(key) + " must be a string");
	return to_wide(value.get<std::string>());
}

} // namespace

bool Settings::load(std::string& error)
{
	std::vector<std::wstring> tried;
	const std::wstring path = find_config_file(FILE_NAME, &tried);
	if (path.empty()) {
		error = to_utf8(FILE_NAME) + std::string(" not found; looked in:");
		for (const std::wstring& candidate : tried)
			error += "\n    " + to_utf8(candidate);
		return false;
	}
	return load_file(path, error);
}

bool Settings::load_file(const std::wstring& path, std::string& error)
{
	std::ifstream file {path};
	if (!file.is_open()) {
		error = "cannot open " + to_utf8(path);
		return false;
	}

	json settings;
	try {
		settings = json::parse(file);
	}
	catch (const json::parse_error& e) {
		error = "failed to parse " + to_utf8(path) + ":\n    " + e.what();
		return false;
	}

	EveWindowMatch eve_window;
	std::wstring image_dir;
	int64_t frame_rate   {0};
	int64_t min_margine  {0};
	double  threshold    {0.0};
	try {
		eve_window.class_name   = read_string(settings, KEY_WINDOW_CLASS);
		eve_window.title_prefix = read_string(settings, KEY_TITLE_PREFIX);
		image_dir               = read_string(settings, KEY_IMAGE_DIR);

		const json& rate = settings.at(KEY_FRAME_RATE);
		if (!rate.is_number_integer())
			throw std::runtime_error(
				std::string(KEY_FRAME_RATE) + " must be a whole number"
			);
		frame_rate = rate.get<int64_t>();

		const json& certainty = settings.at(KEY_THRESHOLD);
		if (!certainty.is_number())
			throw std::runtime_error(
				std::string(KEY_THRESHOLD) + " must be a number"
			);
		threshold = certainty.get<double>();

		const json& margine = settings.at(KEY_MIN_MARGINE);
		if (!margine.is_number_integer())
			throw std::runtime_error(
				std::string(KEY_MIN_MARGINE) + " must be a whole number"
			);
		min_margine = margine.get<int64_t>();
	}
	catch (const json::out_of_range&) {
		// at() names the missing key in its message, but not helpfully
		error = "missing setting in " + to_utf8(path)
		      + "; required keys are "
		      + KEY_WINDOW_CLASS + ", "
		      + KEY_TITLE_PREFIX + ", "
		      + KEY_FRAME_RATE   + ", "
		      + KEY_IMAGE_DIR    + ", "
		      + KEY_THRESHOLD    + ", "
		      + KEY_MIN_MARGINE;
		return false;
	}
	catch (const std::exception& e) {
		error = std::string("bad setting in ") + to_utf8(path)
		      + ": " + e.what();
		return false;
	}

	// The one setting that may be left out, since the menu came later than
	// the file did. Spelt out rather than a key code, so it can be changed
	// by whoever has to press it.
	std::string spelling {MENU_HOTKEY_DEFAULT};
	const auto typed = settings.find(KEY_MENU_HOTKEY);
	if (settings.end() != typed) {
		if (!typed->is_string()) {
			error = std::string("bad setting in ") + to_utf8(path) + ": "
			      + KEY_MENU_HOTKEY + " must be a string";
			return false;
		}
		spelling = typed->get<std::string>();
	}
	Hotkey menu_hotkey;
	std::string trouble;
	if (!parse_hotkey(spelling, menu_hotkey, trouble)) {
		error = std::string("bad setting in ") + to_utf8(path) + ": "
		      + KEY_MENU_HOTKEY + " " + trouble;
		return false;
	}

	// Without either criterion no window could ever match.
	if (eve_window.class_name.empty() && eve_window.title_prefix.empty()) {
		error = std::string("both ") + KEY_WINDOW_CLASS + " and "
		      + KEY_TITLE_PREFIX + " are empty in " + to_utf8(path);
		return false;
	}
	if (frame_rate < 1 || frame_rate > static_cast<int64_t>(MAX_FRAME_RATE)) {
		error = std::string(KEY_FRAME_RATE) + " must be between 1 and "
		      + std::to_string(MAX_FRAME_RATE)
		      + " (got " + std::to_string(frame_rate) + ")";
		return false;
	}
	if (threshold <= 0.0 || threshold > 1.0) {
		error = std::string(KEY_THRESHOLD)
		      + " must be greater than 0 and at most 1 (got "
		      + std::to_string(threshold) + ")";
		return false;
	}
	if (image_dir.empty()) {
		error = std::string(KEY_IMAGE_DIR) + " is empty in " + to_utf8(path);
		return false;
	}
	// Zero would leave a search window exactly the size of the pattern,
	// with no room for anything to have moved a pixel.
	if (min_margine < 1 || min_margine > MAX_MIN_MARGINE) {
		error = std::string(KEY_MIN_MARGINE) + " is in pixels and must be "
		      + "between 1 and " + std::to_string(MAX_MIN_MARGINE)
		      + " (got " + std::to_string(min_margine) + ")";
		return false;
	}

	m_eve_window         = std::move(eve_window);
	m_capture_frame_rate = static_cast<unsigned>(frame_rate);
	m_detect_threshold   = threshold;
	m_min_margine        = static_cast<int>(min_margine);
	m_menu_hotkey        = menu_hotkey;
	// a relative image folder is meant relative to the settings file,
	// not to whatever directory the app happens to be started from
	m_image_dir          = join_path(directory_of(path), image_dir);
	m_source             = path;
	return true;
}
