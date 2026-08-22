/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageLibrary - the set of patterns the detector can look for, described
	by the library file eve_config.json names, and stored as PNG files.

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

enum FixedDirections : unsigned {
	FIXED_NONE = 0,
	FIXED_X    = 1,
	FIXED_Y    = 2,
	FIXED_XY   = FIXED_X | FIXED_Y
};

struct ImagePattern {
	// A search margin the library file did not give. Negative, so it can
	// never be taken for a fraction somebody asked for.
	static constexpr double MARGINE_UNSET = -1.0;

	// Patterns whose sizes differ by more than this are probably not the
	// lookalikes somebody meant to declare, so they get a warning.
	static constexpr double SIMILAR_SIZE_SPREAD = 0.25;

	std::string  name;     // key the 'detect' command uses
	std::string  comment;  // optional, printed by the 'images' command
	eb::Images   similar;  // other patterns that looks very similar
	double       threshold        {0.0};        // detection certainty specific to the pattern
	unsigned     fixed_directions {FIXED_NONE}; // expected position on screen

	cv::Mat      image,  // CV_32FC3, BGR, values 0..1
	             mask;   // CV_32FC3 weights; empty when the PNG is fully opaque

	double       search_margine   {MARGINE_UNSET},
	             search_margine_x {MARGINE_UNSET},
	             search_margine_y {MARGINE_UNSET},
	             fit_weight       {0.0};

	int  width()   const noexcept { return image.cols;                  }
	int  height()  const noexcept { return image.rows;                  }
	int  longest() const noexcept { return std::max(width(), height()); }
	bool masked()  const noexcept { return !mask.empty();               }
	bool fixed_x() const noexcept { return fixed_directions & FIXED_X;  }
	bool fixed_y() const noexcept { return fixed_directions & FIXED_Y;  }

	static bool margine_given(double fraction) noexcept { return fraction >= 0.0; }
};

class ImageLibrary {
public:
	static constexpr size_t NOT_FOUND {static_cast<size_t>(-1)};
	static constexpr int    UNKNOWN   {-1};

	inline static const cv::Point NEVER_SEEN {UNKNOWN, UNKNOWN};

	bool load(
		const std::wstring& file,
		const std::wstring& dir,
		double              default_threshold,
		std::string&        error
	);
	bool load_file(
		const std::wstring& path,
		const std::wstring& dir,
		double              default_threshold,
		std::string&        error
	);

	const  ImagePattern& operator()(size_t image) const;
	       size_t        index(const std::string& name) const;

	       cv::Point     last_hit(size_t image) const;
	       bool          set_last_hit(size_t image, const cv::Point& corner);
	       void          forget_hits();

	       std::string   name_list() const;
	       std::string   names_text(const eb::Images& images) const;
	static std::string   fixed_directions_text(unsigned directions);

	static bool seen   (const cv::Point& corner) noexcept;
	static bool located(const cv::Point& corner) noexcept;
	static bool boxable(const ImagePattern& pattern, const cv::Point& corner) noexcept;

	const std::wstring&              source_path() const noexcept;
	const std::wstring&              directory()   const noexcept;
	const std::vector<std::string>&  warnings()    const noexcept;
	const std::vector<ImagePattern>& patterns()    const noexcept;
	      bool                       empty()       const noexcept;
	      size_t                     size()        const noexcept;

private:
	// Turns every SIMILAR name into an index, makes the relation
	// work both ways, and reports what it had to correct.
	bool link_similar(
		std::vector<ImagePattern>&                   patterns,
		const std::map<std::string, size_t>&         index,
		const std::vector<std::vector<std::string>>& named,
		const std::wstring&                          path,
		std::string&                                 error
	);

	std::wstring                  m_source;
	std::wstring                  m_directory;
	std::vector<std::string>      m_warnings;
	std::vector<ImagePattern>     m_patterns;
	std::vector<cv::Point>        m_last_hits;
	std::map<std::string, size_t> m_index;

	mutable std::mutex m_hits_mutex; // guards m_last_hits only
};

inline const ImagePattern& ImageLibrary::operator()(size_t image) const
{
	return m_patterns[image];
}

inline size_t ImageLibrary::index(const std::string& name) const
{
	const auto found = m_index.find(name);
	return m_index.end() == found ? NOT_FOUND : found->second;
}

inline cv::Point ImageLibrary::last_hit(size_t image) const
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	return m_last_hits[image];
}

// True when the pattern had not been seen there before, which is what makes
// the position worth writing down.
inline bool ImageLibrary::set_last_hit(size_t image, const cv::Point& corner)
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	cv::Point& hit = m_last_hits[image];
	if (hit == corner) return false;
	hit = corner;
	return true;
}

inline void ImageLibrary::forget_hits()
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	m_last_hits.assign(m_patterns.size(), NEVER_SEEN);
}

// "X", "Y", "XY" or "-", as written in the library file.
inline std::string ImageLibrary::fixed_directions_text(unsigned directions)
{
	std::string text;
	if (0 != (directions & FIXED_X)) text += 'X';
	if (0 != (directions & FIXED_Y)) text += 'Y';
	return text.empty() ? "-" : text;
}

// True when anything at all is remembered. Half a corner is possible: the
// position cache keeps only the axes a pattern stays in place along, so a
// position restored at startup may give only one of them.
inline bool ImageLibrary::seen(const cv::Point& corner) noexcept
{
	return corner.x > UNKNOWN || corner.y > UNKNOWN;
}

// True when the whole corner is known. A click needs that much to aim at a
// pattern. A search near it needs less.
inline bool ImageLibrary::located(const cv::Point& corner) noexcept
{
	return corner.x > UNKNOWN && corner.y > UNKNOWN;
}

// True when a quick search has what it needs: the pattern keeps its place
// along at least one axis, and every axis it keeps is known.
inline bool ImageLibrary::boxable(
	const ImagePattern& pattern, const cv::Point& corner) noexcept
{
	if (FIXED_NONE == pattern.fixed_directions
	 || (pattern.fixed_x() && corner.x <= UNKNOWN)
	 || (pattern.fixed_y() && corner.y <= UNKNOWN))
		return false;
	return true;
}

inline const std::wstring& ImageLibrary::source_path() const noexcept
	{ return m_source; }
inline const std::wstring& ImageLibrary::directory() const noexcept
	{ return m_directory; }
inline const std::vector<std::string>& ImageLibrary::warnings() const noexcept
	{ return m_warnings; }
inline const std::vector<ImagePattern>& ImageLibrary::patterns() const noexcept
	{ return m_patterns; }
inline bool ImageLibrary::empty() const noexcept
	{ return m_patterns.empty(); }
inline size_t ImageLibrary::size() const noexcept
	{ return m_patterns.size(); }
