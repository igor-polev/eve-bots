/*
	EVE bots for Windows.
	Author: Igor Polev.

	PositionCache - where patterns were last seen, kept across restarts.

	Finding a pattern the first time means scanning the whole frame, which
	costs seconds; every search afterwards looks at a small box around the
	remembered position. This file carries that memory over between runs.

	A remembered position only means anything for the client it was taken
	from, so each set is filed under the character name and the frame size
	and used only when both match. Sets for other clients are left in the
	file untouched, so switching back and forth costs nothing.

	Only the axes a pattern actually holds still along are written: the free
	one would change with almost every detection and rewrite the file for
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

	// Reads path, which need not exist: an absent cache is the normal state
	// of a first run. False and error when the file is there but unreadable;
	// the cache is then empty and written afresh, since it regenerates
	// itself in one detection per pattern.
	bool load(const std::wstring& path, std::string& error);

	const std::wstring& source_path() const noexcept { return m_path; }

	struct Follow {
		bool changed  {false};  // a different client than the one followed
		int  restored {0};      // positions handed back to the library
	};

	// Points the cache at one client: positions are filed under this
	// character and frame size from now on, and whatever was filed under the
	// same pair before is handed to the library. What the library holds from
	// another client is forgotten first. Following the same client twice
	// changes nothing - what the library holds is fresher than the file.
	Follow follow(
		ImageLibrary&      images,
		const std::string& character,
		int                width,
		int                height
	);

	// Records where a pattern was just found, writing the file only when
	// that changes something worth keeping: a pattern with no
	// FIXED_DIRECTIONS is not kept, and one that moved along a free axis
	// costs nothing. True when the file was written.
	bool store(const ImagePattern& pattern, const cv::Point& corner);

	// What went wrong the last time the file was written, empty when nothing
	// did. Writing happens on whichever thread searched, which cannot
	// politely interrupt the console, so it is kept here for 'status'.
	std::string last_error() const;

	// One line for 'status': which client is being followed and how much is
	// remembered for it.
	std::string state_text() const;

private:
	// A remembered corner, either coordinate of which may be UNKNOWN
	// because the pattern does not hold still along that axis.
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
		// by pattern name, not by position: the library may be edited
		// between runs, and a name outlives a reordering
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
