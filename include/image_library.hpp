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
// interface is fixed to a panel, so once an element has been found it appears
// at the same coordinates again. That lets the detector search a small box
// instead of the whole frame.
enum FixedDirections : unsigned {
	FIXED_NONE = 0,
	FIXED_X    = 1,
	FIXED_Y    = 2,
	FIXED_XY   = FIXED_X | FIXED_Y
};

// "X", "Y", "XY" or "-", as written in eve_images.json.
std::string fixed_directions_text(unsigned directions);

struct ImagePattern {
	// A search margin that eve_images.json did not give. Negative, so it can
	// never be taken for a fraction somebody asked for.
	static constexpr double MARGINE_UNSET = -1.0;
	static bool margine_given(double fraction) noexcept
		{ return fraction >= 0.0; }

	// Patterns whose sizes differ by more than this are probably not the
	// lookalikes somebody meant to declare, so they get a warning.
	static constexpr double SIMILAR_SIZE_SPREAD = 0.25;

	std::string  name;     // key the 'detect' command uses
	std::wstring file;     // path as written in eve_images.json
	std::wstring path;     // where the file was actually read from
	std::string  comment;  // optional, for humans only
	double       threshold {0.0}; // certainty a match must reach to count
	unsigned     fixed_directions {FIXED_NONE};

	// How far past the pattern a quick search looks, as a fraction of the
	// pattern's own size. A per axis value is measured against that axis,
	// the shared one against the longer side. All are optional:
	// ImageDetector fills in its default for each MARGINE_UNSET.
	double search_margine   {MARGINE_UNSET};
	double search_margine_x {MARGINE_UNSET};
	double search_margine_y {MARGINE_UNSET};

	// Patterns this one can be confused with, by index in the library. The
	// correlation used by the search measures shape and does not see
	// colour, so a match is kept only after it is compared with these and
	// none of them fits the same pixels better. The relation works both
	// ways: naming it on one of the two patterns is enough.
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

// Patterns are addressed by their index in the library. A name costs a map
// lookup, so index() resolves it once and the index is passed around after
// that.
class ImageLibrary {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_images.json";

	// What index() returns when there is no pattern with that name.
	static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

	// A coordinate that nothing is known about. A hit is the top left
	// corner of a match, so a real one can be 0 but never negative.
	static constexpr int UNKNOWN = -1;
	// Means a pattern that has never been found.
	inline static const cv::Point NEVER_SEEN {UNKNOWN, UNKNOWN};

	// True when anything at all is remembered. Half a corner is possible:
	// the position cache keeps only the axes a pattern stays in place
	// along, so a position restored at startup may give only one of them.
	static bool seen(const cv::Point& corner) noexcept
		{ return corner.x > UNKNOWN || corner.y > UNKNOWN; }

	// True when the whole corner is known. A click needs that much to aim
	// at a pattern. A search near it needs less.
	static bool located(const cv::Point& corner) noexcept
		{ return corner.x > UNKNOWN && corner.y > UNKNOWN; }

	// True when a quick search has what it needs: the pattern keeps its
	// place along at least one axis, and every axis it keeps is known. The
	// other axes are never read, so half a corner is enough for a pattern
	// that stays in place along one axis only.
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
	// default_threshold is used for patterns that give none of their own.
	// Returns false and fills error on failure.
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

	// Path the description was read from, empty until it is loaded.
	const std::wstring& source_path() const noexcept { return m_source; }
	const std::wstring& directory()   const noexcept { return m_directory; }

	// Problems that loading worked around, but that should still be fixed
	// in the file. Empty when there was nothing to say.
	const std::vector<std::string>& warnings() const noexcept
		{ return m_warnings; }

	bool   empty() const noexcept { return m_patterns.empty(); }
	size_t size()  const noexcept { return m_patterns.size(); }
	const std::vector<ImagePattern>& patterns() const noexcept
		{ return m_patterns; }

	// Index of a named pattern, NOT_FOUND when there is none. This is the
	// only lookup that reports a bad name, so everything that takes a name
	// from outside, such as a command line, goes through it.
	size_t index(const std::string& name) const;

	// The pattern itself. The index is not checked, as in vector::operator[].
	const ImagePattern& operator()(size_t image) const
		{ return m_patterns[image]; }

	// Comma separated list of the names, for error messages.
	std::string name_list() const;

	// "'jump'", "'jump' or 'dock'", "'a', 'b' or 'c'", for messages about
	// something that covered more than one pattern.
	std::string names_text(const eb::Images& images) const;

	// Where a pattern was seen last, or NEVER_SEEN. It is kept here and not
	// in the detector, so that it lives longer than one search, and it is
	// never cleared: a remembered corner is still the best guess for where
	// an element will appear again, even after a search that missed. The
	// thread that searched writes it and the console reads it, so both
	// take a lock.
	cv::Point last_hit(size_t image) const;

	// Returns true when the remembered corner really moved.
	bool set_last_hit(size_t image, const cv::Point& corner);

	// Drops every remembered position. Used when capture moves to another
	// client, where the panels are in different places.
	void forget_hits();

private:
	// Turns every SIMILAR name into an index, makes the relation work both
	// ways, and reports what it had to correct.
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
