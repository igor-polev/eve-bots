/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector - searches captured frames for library patterns.

	A search runs on the thread that asked for it, one at a time: the frame
	is shared and the work is CPU bound, so two at once would only slow each
	other down.

	What was searched for is kept, one pattern at a time, for as long as
	capture keeps handing back the frame it was searched in. The same
	question about the same pixels cannot come to a different answer, so it
	is looked up rather than asked again - which is what lets a program find
	a button and then click it as two searches instead of carrying the
	position between them.

	Per pattern rather than per request, because a request naming several is
	several searches of one pattern each: asking for a gate and then for a
	gate or a station reuses what was learnt about the gate.
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
		PositionCache& cache
	);
	ImageDetector(const ImageDetector&)            = delete;
	ImageDetector& operator=(const ImageDetector&) = delete;

	// Slack in pixels along each axis for one pattern: its own fraction
	// where it named one, MIN_MARGINE where it did not.
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
		const eb::Images& images,
		Detection&        result,
		std::string&      error,
		SearchScope       scope    = SearchScope::BOX_THEN_FULL,
		int               max_hits = 1
	);

private:
	// Smallest slack any search window gets: the floor under a pattern's own
	// margin, and the room a candidate is allowed when it is weighed against
	// the patterns it could be confused with. A couple of pixels of slack
	// around a small icon is no slack at all.
	static constexpr int MIN_MARGINE = 4;

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

	// One pattern's share of a search already made, kept per pattern rather
	// than per request: a search for several is several searches of one
	// pattern each, and any of them may answer a later request that names
	// that pattern among others.
	struct Remembered {
		size_t        image {0};
		int           max_hits {0};
		// The question this answers, which is not always the one that was
		// asked: a pattern whose box missed under a box-then-full request
		// while another pattern answered never had its full pass, so what
		// it settles is the box alone.
		SearchScope   scope {SearchScope::NONE};
		PatternSearch searched;  // and what was actually done for it
		std::vector<DetectionHit> hits;

		bool answers(size_t wanted, int count, SearchScope asked) const
		{
			if (image != wanted || max_hits != count) return false;
			if (scope == asked) return true;
			// A miss only ever answers its own question. A hit found in the
			// box answers either question that looks there, since the box
			// is where a box-then-full search looks first.
			if (hits.empty() || SearchScope::BOX != searched.scope)
				return false;
			return SearchScope::BOX == asked
			    || SearchScope::BOX_THEN_FULL == asked;
		}
	};

	// Best first, whichever pattern a hit came from, and no more of them
	// than were asked for.
	static void rank_hits(std::vector<DetectionHit>& hits, int max_hits);

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

	std::mutex m_mutex;  // serialises callers, and guards everything below

	Frame m_frame;
	// Everything already searched for in m_frame. Only ever appended to and
	// read through, and thrown away whole when the frame changes, so a
	// vector is the container for it: contiguous to scan, one allocation to
	// grow, and clear() keeps the buffer for the next frame to fill.
	std::vector<Remembered> m_memo;
};

using Scope = ImageDetector::SearchScope;