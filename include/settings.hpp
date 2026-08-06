/*
	EVE bots for Windows.
	Author: Igor Polev.

	Settings - eve_config.json configuration file.
*/

#pragma once
#include <string>

#include "window_finder.hpp"

class Settings {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_config.json";

	// Highest capture rate the config file may ask for.
	static constexpr unsigned MAX_FRAME_RATE = 240;
	// Widest MIN_MARGINE worth allowing; past this a "small" window is
	// most of the frame and the searches it guards stop being cheap.
	static constexpr int MAX_MIN_MARGINE = 256;

	// Looks for FILE_NAME in the working directory, then next to the
	// executable. On failure returns false and fills error.
	bool load(std::string& error);

	// Loads a specific file. On failure returns false and fills error.
	bool load_file(const std::wstring& path, std::string& error);

	// Path the settings were actually read from; empty until loaded.
	const std::wstring& source_path() const noexcept { return m_source; }

	const EveWindowMatch& eve_window() const noexcept { return m_eve_window; }
	unsigned capture_frame_rate() const noexcept { return m_capture_frame_rate; }

	// Folder holding the pattern images, resolved against the settings file.
	const std::wstring& image_dir() const noexcept { return m_image_dir; }
	// Match certainty a hit must reach when a pattern names no threshold.
	double detect_threshold() const noexcept { return m_detect_threshold; }
	// Smallest slack, in pixels, any search window is given: the floor
	// under a FIXED_DIRECTIONS margin, and the room a candidate is allowed
	// when it is weighed against the patterns it could be confused with.
	int min_margine() const noexcept { return m_min_margine; }

private:
	std::wstring   m_source;
	EveWindowMatch m_eve_window;
	unsigned       m_capture_frame_rate {0};
	std::wstring   m_image_dir;
	double         m_detect_threshold {0.0};
	int            m_min_margine {0};
};
