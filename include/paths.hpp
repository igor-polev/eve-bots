/*
	EVE bots for Windows.
	Author: Igor Polev.

	Filesystem helpers shared by the configuration files.

	Config files are looked for in the working directory first and next to
	the executable second. So the app runs from the source tree or from an
	installed copy without any change.
*/

#pragma once

#include <string>
#include <vector>

// Directory holding the running executable, without a trailing separator.
std::wstring exe_directory();

// Directory part of path, without a trailing separator. L"." when path
// has no directory at all.
std::wstring directory_of(const std::wstring& path);

// True for paths that name a location on their own: C:\dir, \\server\share.
bool is_absolute_path(const std::wstring& path);

// Joins base and tail, leaving tail alone when it is already absolute.
std::wstring join_path(const std::wstring& base, const std::wstring& tail);

// True if path names an existing file (not a directory).
bool file_exists(const std::wstring& path);

// Looks for name in the working directory, then next to the executable.
// Returns the first path that exists, or an empty string. When tried is
// given, it collects the paths that were tried, so the caller can say
// where it looked.
std::wstring find_config_file(
	const std::wstring& name, std::vector<std::wstring>* tried = nullptr
);
