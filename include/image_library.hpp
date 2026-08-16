/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageLibrary - the set of patterns the detector can look for, described
	by eve_images.json and stored as PNG files.

	Patterns are used at their stored size, so reference images must be cut
	from a screenshot taken at the resolution the bot will run at. An alpha
	channel becomes a mask, so only the opaque part has to match - that is
	how a button is recognised without its changing background.
*/

#pragma once

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "common_defs.hpp"

// Axes along which a pattern normally keeps its place. Most of the EVE user
// interface is pinned to a panel, so once an element has been found it turns
// up at the same coordinates again - which lets the detector look at a small
// box instead of the whole frame.
enum FixedDirections : unsigned {
	FIXED_NONE = 0,
	FIXED_X    = 1,
	FIXED_Y    = 2,
	FIXED_XY   = FIXED_X | FIXED_Y
};

// "X", "Y", "XY" or "-", as written in eve_images.json.
std::string fixed_directions_text(unsigned directions);

struct ImagePattern {
	// A search margin eve_images.json did not name. Negative, so it can
	// never be mistaken for a fraction somebody actually asked for.
	static constexpr double MARGINE_UNSET = -1.0;
	static bool margine_given(double fraction) noexcept
		{ return fraction >= 0.0; }

	// Patterns whose sizes differ by more than this are unlikely to be
	// the lookalikes somebody meant to declare, and get a warning.
	static constexpr double SIMILAR_SIZE_SPREAD = 0.25;

	std::string  name;     // key the 'detect' command uses
	std::wstring file;     // path as written in eve_images.json
	std::wstring path;     // where the file was actually read from
	std::string  comment;  // optional, for humans only
	double       threshold {0.0}; // certainty a match must reach to count
	unsigned     fixed_directions {FIXED_NONE};

	// How far past the pattern a quick search looks, as a fraction of the
	// pattern's own size. The per axis ones measure against that axis, the
	// shared one against the longer side. All optional - ImageDetector
	// fills in its default for whichever are MARGINE_UNSET.
	double search_margine   {MARGINE_UNSET};
	double search_margine_x {MARGINE_UNSET};
	double search_margine_y {MARGINE_UNSET};

	// Patterns this one can be mistaken for, by position in the library.
	// The correlation the search runs on scores by shape and is blind to
	// hue, so a match is only kept once it has been weighed against these
	// and none of them fits the same pixels better. Symmetric: naming it
	// on either pattern is enough.
	eb::Images similar;

	cv::Mat image;  // CV_32FC3, BGR, values 0..1
	cv::Mat mask;   // CV_32FC3 weights; empty when the PNG is fully opaque

	int  width()   const noexcept { return image.cols; }
	int  height()  const noexcept { return image.rows; }
	int  longest() const noexcept { return std::max(width(), height()); }
	bool masked()  const noexcept { return !mask.empty(); }
	bool fixed_x() const noexcept { return 0 != (fixed_directions & FIXED_X); }
	bool fixed_y() const noexcept { return 0 != (fixed_directions & FIXED_Y); }
};

// Patterns are addressed by their position in the library: a name costs a
// map lookup, so it is resolved once by index() and the index is what gets
// passed around afterwards.
class ImageLibrary {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_images.json";

	// index() answers this when there is no pattern by that name.
	static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

	// A coordinate nothing is known about. A hit is the top left corner of
	// a match, so a real one can be 0 but never negative.
	static constexpr int UNKNOWN = -1;
	// Stands for a pattern that has never been found.
	inline static const cv::Point NEVER_SEEN {UNKNOWN, UNKNOWN};

	// True when anything at all is remembered. Half a corner is possible:
	// the position cache keeps only the axes a pattern holds still along,
	// so a position restored at startup may name just one of them.
	static bool seen(const cv::Point& corner) noexcept
		{ return corner.x > UNKNOWN || corner.y > UNKNOWN; }

	// True when the whole corner is known - what it takes to aim at a
	// pattern, as opposed to merely searching near it.
	static bool located(const cv::Point& corner) noexcept
		{ return corner.x > UNKNOWN && corner.y > UNKNOWN; }

	// True when a quick search has what it needs: the pattern keeps its
	// place along at least one axis, and every axis it keeps is known. The
	// others are never read, so half a corner is enough for a pattern that
	// only holds still along one axis.
	static bool boxable(
		const ImagePattern& pattern, const cv::Point& corner) noexcept
	{
		if (FIXED_NONE == pattern.fixed_directions)            return false;
		if (pattern.fixed_x() && corner.x <= UNKNOWN)          return false;
		if (pattern.fixed_y() && corner.y <= UNKNOWN)          return false;
		return true;
	}

	// Looks for FILE_NAME in the working directory, then next to the
	// executable, and reads every PNG it names from dir.
	// default_threshold applies to patterns that name none of their own.
	// On failure returns false and fills error.
	bool load(
		const std::wstring& dir, double default_threshold, std::string& error
	);

	// Loads a specific description file.
	bool load_file(
		const std::wstring& path,
		const std::wstring& dir,
		double              default_threshold,
		std::string&        error
	);

	// Path the description was read from; empty until loaded.
	const std::wstring& source_path() const noexcept { return m_source; }
	const std::wstring& directory()   const noexcept { return m_directory; }

	// Things put right while loading that the file should still be fixed
	// for. Empty when there was nothing to say.
	const std::vector<std::string>& warnings() const noexcept
		{ return m_warnings; }

	bool   empty() const noexcept { return m_patterns.empty(); }
	size_t size()  const noexcept { return m_patterns.size(); }
	const std::vector<ImagePattern>& patterns() const noexcept
		{ return m_patterns; }

	// Position of a named pattern, NOT_FOUND when there is none. This is
	// the only lookup that reports a bad name, so anything that takes a
	// name from outside - a command line, a bot script - goes through it.
	size_t index(const std::string& name) const;

	// The pattern itself. Like vector::operator[] the index is not checked.
	const ImagePattern& operator()(size_t image) const
		{ return m_patterns[image]; }

	// Comma separated list of the names, for error messages.
	std::string name_list() const;

	// "'jump'", "'jump' or 'dock'", "'a', 'b' or 'c'" - for messages about
	// something that covered more than one pattern.
	std::string names_text(const eb::Images& images) const;

	// Where a pattern was seen last, or NEVER_SEEN. Kept here rather than in
	// the detector so that it outlasts any single search, and never cleared:
	// a remembered corner stays the best guess for where an element will
	// show up again, even after a search that missed. Written from whichever
	// thread searched and read from the console, so both take a lock.
	cv::Point last_hit(size_t image) const;

	// Returns true when the remembered corner actually moved.
	bool set_last_hit(size_t image, const cv::Point& corner);

	// Throws away every remembered position. Used when capture moves to a
	// different client, whose panels are somewhere else entirely.
	void forget_hits();

private:
	// Resolves every SIMILAR name to a position, makes the relation
	// symmetric, and complains about what it had to put right.
	bool link_similar(
		std::vector<ImagePattern>&           patterns,
		const std::map<std::string, size_t>& index,
		const std::vector<std::vector<std::string>>& named,
		const std::wstring&                  path,
		std::string&                         error
	);

	std::wstring m_source;
	std::wstring m_directory;
	std::vector<std::string> m_warnings;
	std::vector<ImagePattern> m_patterns;
	std::map<std::string, size_t> m_index;

	mutable std::mutex     m_hits_mutex;  // guards m_last_hits only
	std::vector<cv::Point> m_last_hits;   // parallel to m_patterns
};
