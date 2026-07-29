/*
	EVE bots for Windows.
	Author: Igor Polev.

	Narrow/wide string conversion helpers.

	The console is switched to UTF-8 in main(), so window titles are
	converted to UTF-8 for display rather than using wcout (mixing
	cout and wcout on the same stream is unreliable).
*/

#pragma once
#include <string>
#include <windows.h>

inline std::string to_utf8(const std::wstring& src)
{
	if (src.empty()) return {};
	int size = WideCharToMultiByte(
		CP_UTF8, 0,
		src.data(), static_cast<int>(src.size()),
		nullptr, 0, nullptr, nullptr
	);
	if (size <= 0) return {};
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(
		CP_UTF8, 0,
		src.data(), static_cast<int>(src.size()),
		result.data(), size, nullptr, nullptr
	);
	return result;
}

inline std::wstring to_wide(const std::string& src)
{
	if (src.empty()) return {};
	int size = MultiByteToWideChar(
		CP_UTF8, 0,
		src.data(), static_cast<int>(src.size()),
		nullptr, 0
	);
	if (size <= 0) return {};
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(
		CP_UTF8, 0,
		src.data(), static_cast<int>(src.size()),
		result.data(), size
	);
	return result;
}
