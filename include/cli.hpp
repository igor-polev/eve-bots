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
#include "mouse_input.hpp"
#include "position_cache.hpp"
#include "program.hpp"
#include "program_params.hpp"
#include "screen_capture.hpp"
#include "settings.hpp"
#include "window_finder.hpp"

class Cli {
public:
	// All four must outlive the Cli object.
	Cli(const Settings& settings, ImageLibrary& images,
	    const ProgramParams& params, PositionCache& positions)
		: m_settings {settings}, m_images {images}, m_params {params},
		  m_positions {positions} {}

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
	void cmd_click(const std::vector<std::string>& args);
	void cmd_programs() const;
	void cmd_run(const std::vector<std::string>& args);
	void cmd_abort();

	// Registers the programs and hands them their parameters. False when a
	// parameter is unusable, which stops the application.
	bool load_programs();
	// Prints the outcome of a run. Called on the program thread.
	void report_program(const std::string& name, const ProgramResult& result);

	void print_windows() const;
	// Files the positions of the window capture has just been pointed at
	// under whoever is logged in there, and hands back what was remembered
	// for that client last time. Prints what it did.
	void follow_positions(const WindowInfo& target);
	// Turns a command line token into a library position, taking either a
	// name or the index 'images' prints. NOT_FOUND when it is neither, the
	// complaint having already been printed.
	size_t resolve_image(const std::string& token) const;

	const Settings&      m_settings;
	ImageLibrary&        m_images;
	const ProgramParams& m_params;
	PositionCache&       m_positions;

	// Result of the last 'find', so 'start <n>' can refer to it.
	std::vector<WindowInfo> m_windows;
	ScreenCapture           m_capture;
	ImageDetector           m_detector;
	ProgramRunner           m_programs;
};
