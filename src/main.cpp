/*
	EVE bots for Windows.
	Author: Igor Polev.

	Entry point.
*/

#include <exception>
#include <iostream>

#include <windows.h>
#include <winrt/base.h>

#include "cli.hpp"
#include "settings.hpp"

int main()
{
	// window titles are printed as UTF-8
	SetConsoleOutputCP(CP_UTF8);

	try {
		// multi-threaded apartment: the capture thread uses WinRT too
		winrt::init_apartment(winrt::apartment_type::multi_threaded);

		Settings settings;
		std::string error;
		if (!settings.load(error)) {
			std::cerr << "   [ERROR] " << error << std::endl;
			return -1;
		}

		Cli cli {settings};
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
