/*
	EVE bots for Windows.
	Author: Igor Polev.

	Settings implementation.
*/

#include <fstream>

#include <windows.h>
#include <nlohmann/json.hpp>

#include "settings.hpp"
#include "text_util.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* KEY_WINDOW_CLASS = "EVE_WINDOW_CLASS_NAME";
constexpr const char* KEY_TITLE_PREFIX = "EVE_WINDOW_TITLE_PREFIX";
constexpr const char* KEY_FRAME_RATE   = "CAPTURE_FRAME_RATE_DEFAULT";

// Directory holding the running executable, without a trailing separator.
std::wstring exe_directory()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		const DWORD copied = GetModuleFileNameW(
			nullptr, path.data(), static_cast<DWORD>(path.size())
		);
		if (0 == copied) return L".";
		if (copied < path.size()) {
			path.resize(copied);
			break;
		}
		path.resize(path.size() * 2); // truncated - retry with more room
	}
	const size_t separator = path.find_last_of(L"\\/");
	return std::wstring::npos == separator ? L"." : path.substr(0, separator);
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
	const std::wstring candidates[] {
		FILE_NAME,                                   // working directory
		exe_directory() + L"\\" + FILE_NAME          // next to the exe
	};

	std::string tried;
	for (const std::wstring& path : candidates) {
		std::string attempt_error;
		if (load_file(path, attempt_error))
			return true;
		// Report the first real problem: a file that exists but is broken
		// is far more interesting than one that is simply absent.
		if (std::ifstream {path}.good()) {
			error = attempt_error;
			return false;
		}
		tried += "\n    " + to_utf8(path);
	}
	error = std::string(to_utf8(FILE_NAME)) + " not found; looked in:" + tried;
	return false;
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
	int64_t frame_rate {0};
	try {
		eve_window.class_name   = read_string(settings, KEY_WINDOW_CLASS);
		eve_window.title_prefix = read_string(settings, KEY_TITLE_PREFIX);

		const json& rate = settings.at(KEY_FRAME_RATE);
		if (!rate.is_number_integer())
			throw std::runtime_error(
				std::string(KEY_FRAME_RATE) + " must be a whole number"
			);
		frame_rate = rate.get<int64_t>();
	}
	catch (const json::out_of_range&) {
		// at() names the missing key in its message, but not helpfully
		error = "missing setting in " + to_utf8(path)
		      + "; required keys are "
		      + KEY_WINDOW_CLASS + ", "
		      + KEY_TITLE_PREFIX + ", "
		      + KEY_FRAME_RATE;
		return false;
	}
	catch (const std::exception& e) {
		error = std::string("bad setting in ") + to_utf8(path)
		      + ": " + e.what();
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

	m_eve_window         = std::move(eve_window);
	m_capture_frame_rate = static_cast<unsigned>(frame_rate);
	m_source             = path;
	return true;
}
