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
#include "settings.hpp"

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
		// multi-threaded apartment: the capture thread uses WinRT too
		winrt::init_apartment(winrt::apartment_type::multi_threaded);

		Settings settings;
		std::string error;
		if (!settings.load(error)) {
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}

		// A bot that cannot see is useless, so a broken image library is
		// as fatal as broken settings - better to say so at startup than
		// to fail at the first detection.
		ImageLibrary images;
		if (!images.load(
				settings.image_dir(), settings.detect_threshold(), error))
		{
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}

		Cli cli {settings, images};
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
