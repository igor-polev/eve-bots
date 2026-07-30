/*
	EVE bots for Windows.
	Author: Igor Polev.

	Filesystem helpers shared by the configuration files.

	Config files are looked up in the working directory first and next to
	the executable second, so the app can be run either from the source
	tree or from an installed copy without editing anything.
*/

#pragma once
#include <string>
#include <vector>

// Directory holding the running executable, without a trailing separator.
std::wstring exe_directory();

// Directory part of path, without a trailing separator;
// L"." when path holds no directory at all.
std::wstring directory_of(const std::wstring& path);

// True for paths that name a location on their own: C:\dir, \\server\share.
bool is_absolute_path(const std::wstring& path);

// Joins base and tail, leaving tail alone when it is already absolute.
std::wstring join_path(const std::wstring& base, const std::wstring& tail);

// True if path names an existing file (not a directory).
bool file_exists(const std::wstring& path);

// Looks for name in the working directory, then next to the executable.
// Returns the first path that exists, or an empty string. When tried is
// given it collects the candidates, so callers can say where they looked.
std::wstring find_config_file(
	const std::wstring& name, std::vector<std::wstring>* tried = nullptr
);
