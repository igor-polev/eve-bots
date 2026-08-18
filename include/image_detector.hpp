/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector - looks for library patterns in captured frames.

	A search runs on the thread that asked for it, one search at a time. The
	frame is shared and the work is CPU bound, so two searches at once would
	only make both of them slower.

	Every search is kept while capture keeps handing back the same frame. The
	same question about the same pixels must give the same answer, so the
	answer is looked up instead of searched for again. That is what lets a
	program find a button and then click it as two searches: the second one
	costs nothing.

	Searches are kept per pattern, not per request. A request for several
	patterns is several searches of one pattern each, so a search for a gate
	can also answer a later request for a gate or a station. What decides
	whether a kept search can be used is the area it covered and how many
	hits it had room for, not the name of the request that made it.
*/

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "frame.hpp"
#include "image_library.hpp"
#include "position_cache.hpp"
#include "screen_capture.hpp"

class ImageDetector {
public:
	// ---- what a search is asked for, and what it comes to ----------------

	// What a request asks for: BOX looks only around the remembered
	// position, FULL looks at the whole frame, BOX_THEN_FULL tries the box
	// and then the frame. NONE is never a request, only an answer: the
	// pattern was not looked for, because another one was found first.
	enum class SearchScope {
		NONE,
		FULL,
		BOX,
		BOX_THEN_FULL
	};

	struct DetectionHit {
		size_t    image {0};       // which pattern matched, by library index
		cv::Point at;              // top left corner, in frame pixels
		double    certainty {0.0}; // 0..1, how well the shape matched
		double    fit {0.0};       // 0..1 RMS pixel difference, lower is closer
	};

	// What was done for one pattern in a search. Each pattern keeps its
	// place along its own axes, so each has its own box, or none.
	struct PatternSearch {
		size_t      image {0};
		SearchScope scope {SearchScope::NONE}; // where this one was looked for
		cv::Rect    box;  // the quick box, empty unless scope names one
	};

	struct Detection {
		std::vector<DetectionHit>  hits;
		std::vector<PatternSearch> searched;
		bool remembered {false};
	};

	// All three must live longer than the detector. The library is not
	// const: a search that finds a pattern writes down where it was, and
	// that is what makes the next search quick.
	ImageDetector(
		ImageLibrary&  library,
		ScreenCapture& capture,
		PositionCache& cache
	);
	ImageDetector(const ImageDetector&)            = delete;
	ImageDetector& operator=(const ImageDetector&) = delete;

	// Margin in pixels along each axis for one pattern: the pattern's own
	// fraction if it has one, MIN_MARGINE if it has none.
	cv::Point search_margines(const ImagePattern& pattern) const;

	/*
		Looks for the patterns in the current frame and reports what it
		found. Returns false and fills error when the search cannot be made:
		capture is not running, no frame yet, or a pattern larger than the
		frame. result is then empty. Finding nothing is not an error: it
		returns true with no hits.

		Several patterns mean "any of these". Each of the max_hits reported
		may come from any of them, and every hit says which pattern it is.
		This is not the same as searching for them one by one: the boxes of
		all patterns are searched before the whole frame is, so one pattern
		that moved cannot cost the others their quick search.

		A quick search needs a box, and a box needs a pattern that stays in
		place along an axis and has been seen there once. A pattern with no
		box is looked for in the whole frame, whatever scope was asked for.
		Only that pattern: the others keep their quick search.
	*/
	bool detect(
		const eb::Images& images,
		Detection&        result,
		std::string&      error,
		SearchScope       scope    = SearchScope::BOX_THEN_FULL,
		int               max_hits = 1
	);

private:
	// Smallest margin any search window gets. It is the lowest value a
	// pattern's own margin can take, and the room a candidate gets when it
	// is compared with the patterns it can be confused with. Two pixels
	// around a small icon are as good as none.
	static constexpr int MIN_MARGINE {4};

	// Margin a quick search leaves around the remembered position when
	// eve_images.json names none, as a part of the pattern's longer side.
	static constexpr double SEARCH_MARGINE_DEFAULT {0.1};

	// Correlation values above this cannot happen and mean the arithmetic
	// broke down. See the comment in search_area().
	static constexpr double MAX_VALID_CERTAINTY {1.001};

	// Means "could not be judged". Worse than any real difference, which
	// cannot be more than 1 on the images' own scale.
	static constexpr double WORST_FIT {2.0};

	// Part of the pattern size blanked around a hit before the next hit is
	// looked for, so the same object one pixel over cannot be found twice.
	static constexpr int SUPPRESSION_DIVISOR {4};

	// Takes byte colour values to the 0..1 scale the patterns are kept on.
	static constexpr double COLOUR_SCALE {1.0 / 255.0};

	// One pattern's share of a search already made. Kept per pattern, not
	// per request: a request for several patterns is several searches of one
	// pattern each, and any of them may answer a later request that names
	// that pattern together with others.
	struct Remembered {
		size_t      image {0};
		int         max_hits {0};   // how many hits were wanted that time
		// The area really searched, BOX or FULL. The name of the request
		// does not matter later. Only where the pixels were looked at
		// decides what the answer can still be used for.
		SearchScope scope {SearchScope::NONE};
		// The same again in the form the console prints, so that an answer
		// handed back can say which search made it.
		PatternSearch             searched;
		std::vector<DetectionHit> hits;

		// True when the search stopped because no candidate was left, not
		// because it ran out of room. Then nothing above the threshold was
		// left unreported.
		bool exhaustive() const noexcept
			{ return hits.size() < static_cast<size_t>(max_hits); }

		// Whether this can answer a new request, before its hits are cut
		// down to what that request covers. The area searched must be at
		// least the area asked about. The box lies inside the frame, so a
		// whole frame search can answer a box request. A box search says
		// nothing about the rest of the frame, so it can answer a wider
		// request only by having found something in the box.
		bool answers(size_t wanted, int count, SearchScope asked) const
		{
			if (image != wanted || count > max_hits) return false;
			if (SearchScope::BOX  == asked) return true;
			if (SearchScope::FULL == scope) return true;
			return SearchScope::BOX_THEN_FULL == asked && !hits.empty();
		}
	};

	// Best match first, from whichever pattern it came, and no more hits
	// than were asked for.
	static void rank_hits(std::vector<DetectionHit>& hits, int max_hits);

	// Searches one rectangle of the BGRA frame for one pattern. Hits come
	// back in frame coordinates, best match first, and replace what was in
	// the vector. Converts what it needs into m_scene first, so it is not
	// const.
	void search_area(
		const cv::Mat&             captured,
		const cv::Rect&            area,
		size_t                     image,
		int                        max_hits,
		std::vector<DetectionHit>& hits
	);

	// How close this pattern comes to the best position in a window of the
	// scene, as a root mean square pixel difference on the images' own 0..1
	// scale. The mask normalises it, so patterns of different sizes can
	// still be compared. WORST_FIT when the pattern does not fit in the
	// window.
	double best_fit(
		const cv::Mat&      scene,
		const cv::Rect&     window,
		const ImagePattern& pattern
	) const;

	// The box a quick search covers: the pattern at its remembered corner,
	// grown by a margin along each fixed direction, and the whole frame
	// along the directions that are not fixed.
	cv::Rect quick_box(
		const ImagePattern& pattern,
		const cv::Point&    last,
		const cv::Rect&     frame
	) const;

	ImageLibrary&  m_library;
	ScreenCapture& m_capture;
	PositionCache& m_cache;

	std::mutex m_mutex;  // one caller at a time, and guards everything below

	Frame m_frame;
	// The same frame as float BGR, which is what matching needs. It is the
	// size of the whole frame, so a point in it is a point in the frame, but
	// only m_converted holds real pixels. That rectangle grows as searches
	// ask for areas, and a new frame makes all of it stale. Converting is
	// what a search of a small box would otherwise spend most of its time
	// on, so nothing is converted twice while the frame lasts.
	cv::Mat  m_scene;
	cv::Rect m_converted;
	// Every search already made in m_frame. Entries are only added and read,
	// and the whole list is dropped when the frame changes. A vector suits
	// that: one block to scan, one allocation to grow, and clear() keeps the
	// buffer for the next frame.
	std::vector<Remembered> m_memo;
};

namespace eb {
	using Scope = ImageDetector::SearchScope;
};
