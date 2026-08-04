/*
	EVE bots for Windows.
	Author: Igor Polev.

	Cli - interactive console user interface.
*/

#pragma once
#include <string>
#include <vector>

#include "image_detector.hpp"
#include "image_library.hpp"
#include "screen_capture.hpp"
#include "settings.hpp"
#include "window_finder.hpp"

class Cli {
public:
	// settings and images must outlive the Cli object.
	Cli(const Settings& settings, ImageLibrary& images)
		: m_settings {settings}, m_images {images} {}

	// Reads and dispatches commands until 'exit'. Returns process exit code.
	int run();

private:
	// Returns false only when the app should quit.
	bool dispatch(const std::string& line);
	void cmd_help()  const;
	void cmd_find();
	void cmd_start(const std::vector<std::string>& args);
	void cmd_stop();
	void cmd_dump(const std::vector<std::string>& args) const;
	void cmd_status() const;
	void cmd_images() const;
	void cmd_detect(const std::vector<std::string>& args);

	void print_windows() const;

	const Settings& m_settings;
	ImageLibrary&   m_images;

	// Result of the last 'find', so 'start <n>' can refer to it.
	std::vector<WindowInfo> m_windows;
	ScreenCapture           m_capture;
	ImageDetector           m_detector;
};
