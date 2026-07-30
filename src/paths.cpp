/*
	EVE bots for Windows.
	Author: Igor Polev.

	Filesystem helpers implementation.
*/

#include <windows.h>

#include "paths.hpp"

namespace {

constexpr const wchar_t* SEPARATORS = L"\\/";

} // namespace

std::wstring exe_directory()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		const DWORD copied = GetModuleFileNameW(
			nullptr, path.data(), static_cast<DWORD>(path.size())
		);
		if (0 == copied) return L".";
		if (copied < path.size()) {
			path.resize(copied);
			break;
		}
		path.resize(path.size() * 2); // truncated - retry with more room
	}
	return directory_of(path);
}

std::wstring directory_of(const std::wstring& path)
{
	const size_t separator = path.find_last_of(SEPARATORS);
	return std::wstring::npos == separator ? L"." : path.substr(0, separator);
}

bool is_absolute_path(const std::wstring& path)
{
	if (path.size() >= 2 && L':' == path[1])          // C:\dir or C:dir
		return true;
	if (path.size() >= 2 &&                            // \\server\share
		(L'\\' == path[0] || L'/' == path[0]) &&
		(L'\\' == path[1] || L'/' == path[1]))
		return true;
	return false;
}

std::wstring join_path(const std::wstring& base, const std::wstring& tail)
{
	if (tail.empty())               return base;
	if (is_absolute_path(tail))     return tail;
	if (base.empty() || L"." == base) return tail;

	std::wstring joined {base};
	if (std::wstring::npos == std::wstring(SEPARATORS).find(joined.back()))
		joined += L'\\';
	return joined + tail;
}

bool file_exists(const std::wstring& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return INVALID_FILE_ATTRIBUTES != attributes
	    && 0 == (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring find_config_file(
	const std::wstring& name, std::vector<std::wstring>* tried)
{
	const std::wstring candidates[] {
		name,                                    // working directory
		join_path(exe_directory(), name)         // next to the executable
	};
	for (const std::wstring& candidate : candidates) {
		if (tried) tried->push_back(candidate);
		if (file_exists(candidate)) return candidate;
	}
	return {};
}
