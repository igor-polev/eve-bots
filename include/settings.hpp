/*
	EVE bots for Windows.
	Author: Igor Polev.

	Settings - eve_config.json configuration file.
*/

#pragma once

#include <string>

#include "hotkey.hpp"
#include "input_priority.hpp"
#include "program_defaults.hpp"
#include "window_finder.hpp"

class Settings {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_config.json";

	// What brings up the pop-up program menu. The only optional setting: a
	// file written before the menu existed still loads, and an empty string
	// in a newer file turns the menu off.
	static constexpr const char* MENU_HOTKEY_DEFAULT = "Alt+`";

	// Highest capture rate the config file may ask for.
	static constexpr unsigned MAX_FRAME_RATE = 240;
	// A click repeated this many times is no longer a retry: it is a
	// program beating on the interface.
	static constexpr int MAX_ACTION_RETRIES = 20;
	// Every confirmed click a program makes pays this, so five seconds of
	// it is already a route that crawls. Anything longer was meant to be a
	// timeout somewhere else.
	static constexpr int64_t MAX_WAIT_CLICK = 5000;
	// If it had to wait for more than a minute of quiet, the bot would
	// spend more time waiting for a gap than working, and the person at
	// the keyboard would just stop it.
	static constexpr int64_t MAX_USER_PRIORITY_IDLE = 60000;
	// And a program that waits more than ten minutes is not being polite
	// to the user, it has hung.
	static constexpr int64_t MAX_USER_PRIORITY_TIMEOUT = 600000;

	// Looks for FILE_NAME in the working directory, then next to the
	// executable. Returns false and fills error on failure.
	bool load(std::string& error);

	// Loads one named file. Returns false and fills error on failure.
	bool load_file(const std::wstring& path, std::string& error);

	// Path the settings were really read from, empty until they are loaded.
	const std::wstring& source_path() const noexcept { return m_source; }

	const EveWindowMatch& eve_window() const noexcept { return m_eve_window; }
	unsigned capture_frame_rate() const noexcept { return m_capture_frame_rate; }

	// Folder with the pattern images, taken relative to the settings file.
	const std::wstring& image_dir() const noexcept { return m_image_dir; }
	// Certainty a hit must reach when a pattern gives no threshold of its own.
	double detect_threshold() const noexcept { return m_detect_threshold; }
	// Invalid when the file asked for no menu.
	const Hotkey& menu_hotkey() const noexcept { return m_menu_hotkey; }
	// Whether to capture a client when the application starts, without
	// being told to.
	bool autostart_capture() const noexcept { return m_autostart_capture; }
	// What every program starts from.
	const ProgramDefaults& program_defaults() const noexcept
		{ return m_defaults; }
	// How much of the desktop the user keeps while a program is clicking.
	const InputPriority& input_priority() const noexcept
		{ return m_priority; }

private:
	std::wstring    m_source;
	Hotkey          m_menu_hotkey;
	ProgramDefaults m_defaults;
	InputPriority   m_priority;
	EveWindowMatch  m_eve_window;
	unsigned        m_capture_frame_rate {0};
	std::wstring    m_image_dir;
	double          m_detect_threshold {0.0};
	bool            m_autostart_capture {false};
};
