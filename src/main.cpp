/*
	EVE bots for Windows.
	Author: Igor Polev.

	Entry point.
*/

#include <exception>
#include <iostream>

#include <windows.h>
#include <winrt/base.h>
#include <opencv2/core/utils/logger.hpp>

#include "cli.hpp"
#include "image_library.hpp"
#include "paths.hpp"
#include "position_cache.hpp"
#include "program_params.hpp"
#include "settings.hpp"
#include "text_util.hpp"

int main()
{
	// window titles are printed as UTF-8
	SetConsoleOutputCP(CP_UTF8);
	// Clicks are aimed using window geometry, and Windows silently scales
	// every such measurement for a process that has not said it understands
	// DPI. On a display at anything but 100% the coordinates would be off.
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// OpenCV reports every optional parallel backend it fails to load the
	// first time it is used; the console is our user interface, keep it quiet
	cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

	try {
		// multi-threaded apartment: capture is WinRT, and it is pulled from
		// whichever thread asks for a frame
		winrt::init_apartment(winrt::apartment_type::multi_threaded);

		Settings settings;
		std::string error;
		if (!settings.load(error)) {
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}

		// A bot that cannot see is useless, so a broken image library is as
		// fatal as broken settings.
		ImageLibrary images;
		if (!images.load(
				settings.image_dir(), settings.detect_threshold(), error))
		{
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}
		// Put right rather than fatal, but the file should still be fixed.
		for (const std::string& warning : images.warnings())
			std::cout << " [WARNING] " << warning << "\n";

		// Programs carry their own defaults, so this file only overrides
		// them: missing is a note, broken is fatal.
		ProgramParams params;
		if (!params.load(error)) {
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}
		if (!params.loaded()) {
			std::cout << " [WARNING] " << to_utf8(ProgramParams::FILE_NAME)
			          << " not found; programs will use their built in "
			             "defaults.\n";
		}

		// Where patterns were last seen, kept beside the settings file so it
		// lands wherever the bot was installed. This one is written by us
		// and regenerates itself, so a broken one is worth a word but not a
		// refusal to start.
		PositionCache positions;
		if (!positions.load(
				join_path(
					directory_of(settings.source_path()),
					PositionCache::FILE_NAME
				),
				error))
		{
			std::cout << " [WARNING] " << error
			          << "\n           Positions will be learnt afresh and "
			             "the file rewritten.\n";
		}

		Cli cli {settings, images, params, positions};
		return cli.run();
	}
	catch (const winrt::hresult_error& e) {
		std::cerr << "   [ERROR] Unhandled Windows error in main():\n"
		          << to_string(e.message()) << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "   [ERROR] Unhandled exception in main():\n"
		          << e.what() << std::endl;
	}
	catch (...) {
		std::cerr << "   [ERROR] Unknown exception in main().\n";
	}
	return -1;
}
