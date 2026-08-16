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
constexpr const char* KEY_MENU_HOTKEY  = "MENU_HOTKEY";
constexpr const char* KEY_AUTOSTART    = "AUTOSTART_CAPTURE";
constexpr const char* KEY_CONFIRM_TIMEOUT = "CONFIRM_TIMEOUT_DEFAULT";
constexpr const char* KEY_WAIT_CLICK      = "WAIT_CLICK_DEFAULT";
constexpr const char* KEY_ACTION_RETRIES  = "ACTION_RETRIES_DEFAULT";
constexpr const char* KEY_USER_IDLE       = "USER_PRIORITY_IDLE";
constexpr const char* KEY_USER_TIMEOUT    = "USER_PRIORITY_TIMEOUT";

// Reads a required whole-number setting.
int64_t read_whole(const json& settings, const char* key)
{
	const json& value = settings.at(key);
	if (!value.is_number_integer())
		throw std::runtime_error(std::string(key) + " must be a whole number");
	return value.get<int64_t>();
}

// Reads a required yes-or-no setting.
bool read_flag(const json& settings, const char* key)
{
	const json& value = settings.at(key);
	if (!value.is_boolean())
		throw std::runtime_error(
			std::string(key) + " must be true or false"
		);
	return value.get<bool>();
}

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
	int64_t frame_rate      {0};
	int64_t confirm_timeout {0};
	int64_t wait_click      {0};
	int64_t action_retries  {0};
	int64_t user_idle       {0};
	int64_t user_timeout    {0};
	double  threshold       {0.0};
	bool    autostart       {false};
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

		confirm_timeout = read_whole(settings, KEY_CONFIRM_TIMEOUT);
		wait_click      = read_whole(settings, KEY_WAIT_CLICK);
		action_retries  = read_whole(settings, KEY_ACTION_RETRIES);
		user_idle       = read_whole(settings, KEY_USER_IDLE);
		user_timeout    = read_whole(settings, KEY_USER_TIMEOUT);
		autostart       = read_flag(settings, KEY_AUTOSTART);
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
		      + KEY_CONFIRM_TIMEOUT + ", "
		      + KEY_WAIT_CLICK      + ", "
		      + KEY_ACTION_RETRIES  + ", "
		      + KEY_USER_IDLE       + ", "
		      + KEY_USER_TIMEOUT    + ", "
		      + KEY_AUTOSTART;
		return false;
	}
	catch (const std::exception& e) {
		error = std::string("bad setting in ") + to_utf8(path)
		      + ": " + e.what();
		return false;
	}

	// A click that keeps looking forever is a program that never reports a
	// failure.
	if (confirm_timeout < 1) {
		error = std::string(KEY_CONFIRM_TIMEOUT)
		      + " is milliseconds and must be at least 1 (got "
		      + std::to_string(confirm_timeout) + ")";
		return false;
	}
	// No wait at all is meaningful - it says this client keeps up - so only
	// the upper end is guarded. Every confirmed click a program makes pays
	// this, so a long one is not wrong, only slow.
	if (wait_click < 0 || wait_click > MAX_WAIT_CLICK) {
		error = std::string(KEY_WAIT_CLICK)
		      + " is milliseconds and must be between 0 and "
		      + std::to_string(MAX_WAIT_CLICK)
		      + " (got " + std::to_string(wait_click) + ")";
		return false;
	}
	if (action_retries < 0 || action_retries > MAX_ACTION_RETRIES) {
		error = std::string(KEY_ACTION_RETRIES)
		      + " must be between 0 and " + std::to_string(MAX_ACTION_RETRIES)
		      + " (got " + std::to_string(action_retries) + ")";
		return false;
	}

	// Zero idle is meaningful - it is the bot-first mode - so only the
	// upper end needs guarding here.
	if (user_idle < 0 || user_idle > MAX_USER_PRIORITY_IDLE) {
		error = std::string(KEY_USER_IDLE)
		      + " is milliseconds and must be between 0 and "
		      + std::to_string(MAX_USER_PRIORITY_IDLE)
		      + " (got " + std::to_string(user_idle) + ")";
		return false;
	}
	if (user_timeout < 0 || user_timeout > MAX_USER_PRIORITY_TIMEOUT) {
		error = std::string(KEY_USER_TIMEOUT)
		      + " is milliseconds and must be between 0 and "
		      + std::to_string(MAX_USER_PRIORITY_TIMEOUT)
		      + " (got " + std::to_string(user_timeout) + ")";
		return false;
	}
	// A timeout shorter than the quiet it is waiting for can never be
	// satisfied: every click would wait the whole timeout and then go
	// ahead anyway, which is the bot-first mode taken the slow way round.
	if (user_idle > 0 && user_timeout < user_idle) {
		error = std::string(KEY_USER_TIMEOUT) + " is shorter than "
		      + KEY_USER_IDLE + ", so the wait for a quiet moment could "
		      + "never end in one";
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
	m_eve_window         = std::move(eve_window);
	m_capture_frame_rate = static_cast<unsigned>(frame_rate);
	m_detect_threshold   = threshold;
	m_menu_hotkey        = menu_hotkey;
	m_autostart_capture  = autostart;
	m_defaults.CONFIRM_TIMEOUT = eb::Millis {confirm_timeout};
	m_defaults.WAIT_CLICK      = eb::Millis {wait_click};
	m_defaults.ACTION_RETRIES  = static_cast<int>(action_retries);
	m_priority.USER_PRIORITY_IDLE    = eb::Millis {user_idle};
	m_priority.USER_PRIORITY_TIMEOUT = eb::Millis {user_timeout};
	// a relative image folder is meant relative to the settings file,
	// not to whatever directory the app happens to be started from
	m_image_dir          = join_path(directory_of(path), image_dir);
	m_source             = path;
	return true;
}
