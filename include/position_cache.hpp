/*
	EVE bots for Windows.
	Author: Igor Polev.

	PositionCache - where patterns were last seen, kept across restarts.

	Finding a pattern the first time means searching the whole frame, which
	costs seconds. Every search after that looks at a small box around the
	remembered position. This file carries that memory from run to run.

	A remembered position means something only for the client it came from,
	so each set is filed under the character name and the frame size, and is
	used only when both match. Sets for other clients stay in the file
	untouched, so switching between clients costs nothing.

	Only the axes a pattern stays in place along are written. A free axis
	would change with almost every detection and rewrite the file for
	nothing.
*/

#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "image_library.hpp"

class PositionCache {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_positions.json";

	// Reads path. The file need not exist: on a first run there is none.
	// Returns false and fills error when the file is there but cannot be
	// read. The cache is then empty and is written again from the start,
	// because one detection per pattern fills it back up.
	bool load(const std::wstring& path, std::string& error);

	const std::wstring& source_path() const noexcept { return m_path; }

	struct Follow {
		bool changed  {false};  // a different client than the one followed
		int  restored {0};      // positions handed back to the library
	};

	// Points the cache at one client. From now on positions are filed under
	// this character and frame size, and whatever was filed under the same
	// pair before is given to the library. Positions the library holds from
	// another client are forgotten first. Following the same client twice
	// changes nothing, because the library holds newer values than the file.
	Follow follow(
		ImageLibrary&      images,
		const std::string& character,
		int                width,
		int                height
	);

	// Writes down where a pattern was just found. It saves the file only
	// when something worth keeping has changed: a pattern with no
	// FIXED_DIRECTIONS is not kept at all, and a move along a free axis
	// changes nothing. True when the file was written.
	bool store(const ImagePattern& pattern, const cv::Point& corner);

	// What went wrong the last time the file was written, empty when nothing
	// did. The file is written by the thread that searched, and that thread
	// cannot interrupt the console, so the error is kept here for 'status'.
	std::string last_error() const;

	// One line for 'status': which client is followed, and how much is
	// remembered for it.
	std::string state_text() const;

private:
	// A remembered corner. Either coordinate may be UNKNOWN, because the
	// pattern does not stay in place along that axis.
	struct Position {
		int x {ImageLibrary::UNKNOWN};
		int y {ImageLibrary::UNKNOWN};

		bool operator==(const Position& other) const noexcept
			{ return x == other.x && y == other.y; }
	};

	// Everything remembered for one character at one frame size.
	struct Entry {
		std::string character;
		int         width  {0};
		int         height {0};
		// by pattern name, not by index: the library may be edited between
		// runs, and a name survives a change of order
		std::map<std::string, Position> images;
	};

	static constexpr size_t NONE = static_cast<size_t>(-1);

	// Writes the whole file. The caller holds m_mutex.
	bool save(std::string& error) const;

	mutable std::mutex m_mutex;
	std::wstring       m_path;
	std::vector<Entry> m_entries;
	size_t             m_current {NONE};  // entry follow() settled on
	std::string        m_error;
};
