/*
	EVE bots for Windows.
	Author: Igor Polev.

	PNG output via the Windows Imaging Component.
*/

#pragma once
#include <string>

#include "frame.hpp"

// Writes frame to path as a PNG. On failure returns false and fills error.
bool write_png(const Frame& frame, const std::wstring& path, std::string& error);
