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
	// Clicks are aimed with window geometry, and Windows quietly scales
	// every such measurement for a process that has not said it understands
	// DPI. On a display at anything but 100% the coordinates would be wrong.
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// The first time it is used, OpenCV reports every optional parallel
	// backend it could not load. The console is our user interface, so keep
	// it quiet
	cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

	try {
		// multi-threaded apartment: capture is WinRT, and any thread may ask
		// it for a frame
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
		// Worked around and not fatal, but the file should still be fixed.
		for (const std::string& warning : images.warnings())
			std::cout << " [WARNING] " << warning << "\n";

		// Programs carry their own default values, so this file only overrides
		// them. A missing file is a note. A broken one is fatal.
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

		// Where patterns were last seen. It is kept next to the settings file,
		// so it lands where the bot was installed. We write this file
		// ourselves and it fills up again on its own, so a broken one is worth
		// a warning but not a refusal to start.
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
