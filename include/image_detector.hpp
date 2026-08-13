/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector - searches captured frames for library patterns.

	A search runs on the thread that asked for it, one at a time: the frame
	is shared and the work is CPU bound, so two at once would only slow each
	other down.

	Every answered request is kept for as long as capture keeps handing back
	the frame it was answered from. The same question about the same pixels
	cannot come to a different answer, so it is looked up rather than asked
	again - which is what lets a program find a button and then click it as
	two searches instead of carrying the position between them.
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

	// As a request: BOX searches only around the remembered position, FULL
	// only the whole frame, BOX_THEN_FULL falls back from one to the other.
	// NONE is an outcome alone - the pattern was never looked for, another
	// one having answered first.
	enum class SearchScope {
		NONE,
		FULL,
		BOX,
		BOX_THEN_FULL
	};

	struct DetectionHit {
		size_t      image {0};       // which pattern matched, into the library
		cv::Point   at;              // top left corner, in frame pixels
		double      certainty {0.0}; // 0..1, how well the shape matched
		double      fit {0.0};       // 0..1 RMS pixel difference, lower is closer
		SearchScope scope {SearchScope::NONE}; // what the search was asked for
	};

	// One pattern's share of a search. Each keeps its place along its own
	// axes, so each has its own box or none.
	struct PatternSearch {
		size_t      image {0};
		SearchScope scope {SearchScope::NONE}; // where this one was looked for
		cv::Rect    box;          // the quick box, empty unless scope names one
		int         mistaken {0}; // candidates a similar pattern claimed
	};

	struct Detection {
		// Best match first, whichever pattern it came from.
		std::vector<DetectionHit>  hits;
		// One per pattern asked for, in the order they were given.
		std::vector<PatternSearch> searched;
		SearchScope scope {SearchScope::NONE}; // what the search was asked for
		// True when nothing was searched: the same question had been asked
		// of the same frame, and searched says what was done that time.
		bool        remembered {false};
	};

	// All three must outlive the detector. The library is not const: a
	// successful search records where the pattern was found, which is what
	// makes the next one quick.
	ImageDetector(
		ImageLibrary&  library,
		ScreenCapture& capture,
		PositionCache& cache,
		int            min_margine // TODO: make min_margine a class private const, not param
	);
	ImageDetector(const ImageDetector&)            = delete;
	ImageDetector& operator=(const ImageDetector&) = delete;

	// Smallest slack any search window gets, from MIN_MARGINE in the
	// settings.
	int min_margine() const noexcept { return m_min_margine; }

	// Slack in pixels along each axis for one pattern: its own fraction
	// where it named one, min_margine() where it did not.
	cv::Point search_margines(const ImagePattern& pattern) const;

	/*
		Searches the current frame for the patterns and reports what it
		found. Returns false on error - no capture running, no frame, a
		pattern larger than the frame, nothing to be quick about - and fills
		error; result is then empty. Finding nothing is not an error: it
		returns true with no hits.

		Several patterns mean "any of these": each of the max_hits reported
		may come from any of them, and each hit says which. This is not the
		same as searching for them one after another - the boxes of all of
		them are looked at before any full frame search is, so a pattern that
		has moved cannot cost the others their quick pass.
	*/
	bool detect(
		const std::vector<size_t>& images,
		Detection&                 result,
		std::string&               error,
		SearchScope                scope    = SearchScope::BOX_THEN_FULL,
		int                        max_hits = 1
	);

private:
	// Slack a quick search leaves around the remembered position when
	// eve_images.json asks for none, as a fraction of the pattern's longer
	// side.
	static constexpr double SEARCH_MARGINE_DEFAULT = 0.1;

	// Correlation values above this are impossible and mean the arithmetic
	// broke down; see the comment in search_area().
	static constexpr double MAX_VALID_CERTAINTY = 1.001;

	// Stands for "could not be judged": worse than any real difference,
	// which cannot exceed 1 on the images' own scale.
	static constexpr double WORST_FIT = 2.0;

	// Fraction of the pattern size blanked around a hit before looking for
	// the next one, so a second match cannot be the same object shifted by
	// a pixel.
	static constexpr int SUPPRESSION_DIVISOR = 3;

	// A request that has already been answered, and what it came to. The
	// whole request is the key: how many hits were wanted and where they
	// were looked for both change the answer.

	// TODO: Remembered must contain separate images, not list. Even if list was requested, there were several searches for one pattern each and these must be remembered. When a new list search is requested, it should not be checked for the whole list, but for each image separately -- if any is remembered, that will do.
	struct Remembered {
		std::vector<size_t> images;
		int                 max_hits {0};
		SearchScope         scope {SearchScope::NONE};
		Detection           answer;

		bool answers(
			const std::vector<size_t>& wanted, int hits, SearchScope where) const
		{
			return max_hits == hits && scope == where && images == wanted;
		}
	};

	// Searches one rectangle of a BGRA frame for one pattern. Hits come back
	// in frame coordinates, best match first, replacing whatever was in the
	// vector. mistaken counts the candidates a similar pattern explained
	// better.
	void search_area(
		const cv::Mat&             captured,
		const cv::Rect&            area,
		size_t                     image,
		int                        max_hits,
		std::vector<DetectionHit>& hits,
		int&                       mistaken
	) const;

	// Closest this pattern comes to any position in a window of the scene,
	// as a root mean square pixel difference on the images' own 0..1 scale.
	// Normalised by the mask, so patterns of different sizes stay
	// comparable. WORST_FIT when it does not fit in the window.
	double best_fit(
		const cv::Mat&      scene,
		const cv::Rect&     window,
		const ImagePattern& pattern
	) const;

	// The box a quick search covers: the pattern at its remembered corner,
	// grown by a margin along every fixed direction and spanning the whole
	// frame along the others.
	cv::Rect quick_box(
		const ImagePattern& pattern,
		const cv::Point&    last,
		const cv::Rect&     frame
	) const;

	ImageLibrary&  m_library;
	ScreenCapture& m_capture;
	PositionCache& m_cache;
	int            m_min_margine;

	std::mutex m_mutex;  // serialises callers, and guards everything below

	Frame                   m_frame;
	std::vector<Remembered> m_memo;  // everything asked about m_frame
	// TODO: question: why std::vector is used for FIFO buffer m_memo, not a std::deque?
};
