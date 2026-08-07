/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector - searches captured frames for library patterns.

	Matching a pattern against a full ultrawide frame costs tens of
	milliseconds, so it runs on its own thread rather than on whichever
	thread asked for it: the console stays responsive, and a bot program
	will later be able to ask for a detection without stalling capture.

	Callers hand over a request and wait for the answer. Only one search
	runs at a time - the frame is shared and the work is CPU bound, so
	queueing several would slow all of them down.
*/

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "image_library.hpp"
#include "position_cache.hpp"
#include "screen_capture.hpp"

struct DetectionHit {
	size_t    image {0};      // which pattern matched, into the library
	cv::Point at;             // top left corner of the match, in frame pixels
	double    certainty {0.0};   // 0..1, how well the shape matched
	double    fit {0.0};         // 0..1 RMS pixel difference, lower is closer
};

// Which part of the frame a search covered. Reported back because the two
// differ by two orders of magnitude in cost, so a caller wondering why a
// detection was slow needs to see which one it got.
enum class SearchScope {
	NONE,          // not looked for: another pattern had already answered
	FULL,          // the whole frame
	BOX,           // only the box around the remembered position
	BOX_THEN_FULL  // the box missed, so the whole frame followed
};

// One pattern's share of a search. A detection may cover several patterns
// at once, and they need not have been looked for in the same way: each
// keeps its place along its own axes, so each has its own box or none.
struct PatternSearch {
	size_t      image {0};
	SearchScope scope {SearchScope::NONE};
	cv::Rect    box;            // the quick box, empty unless scope names one
	int         mistaken {0};   // candidates a similar pattern claimed
};

struct Detection {
	// Best match first, whichever pattern it came from. Certainties from
	// different patterns are not strictly comparable, but they are all
	// the same kind of number and there is nothing better to sort by.
	std::vector<DetectionHit>  hits;
	// One per pattern the search was asked for, in the order they were
	// given, whether or not it turned anything up - and whether or not it
	// was looked for at all, since one pattern answering spares the rest.
	std::vector<PatternSearch> searched;
};

class ImageDetector {
public:
	ImageDetector() = default;
	~ImageDetector();
	ImageDetector(const ImageDetector&)            = delete;
	ImageDetector& operator=(const ImageDetector&) = delete;

	// How long detect() waits for the thread before giving up. Searching a
	// whole 3440x1392 frame measured 3.6 s unmasked and 10.3 s masked, so
	// this is a safety net against a wedged thread, not a working budget.
	static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT {30000};

	// Slack a quick search leaves around the remembered position when
	// eve_images.json asks for none, as a fraction of the pattern's longer
	// side; and the floor below which no margin may fall, because a couple
	// of pixels of slack around a small icon is no slack at all.
	static constexpr double SEARCH_MARGINE_DEFAULT = 0.1;
	static constexpr int    SEARCH_MARGINE_MIN     = 4;

	// Smallest slack any search window gets, from MIN_MARGINE in the
	// settings. Set before start(); the default only keeps the detector
	// usable if nobody does.
	void set_min_margine(int pixels) noexcept { m_min_margine = pixels; }
	int  min_margine() const noexcept { return m_min_margine; }

	// Where found positions are written so the next session starts with
	// them. Optional; without one the detector still remembers positions
	// for as long as it runs. Set before start(); the cache must outlive
	// the detector.
	void set_cache(PositionCache* cache) noexcept { m_cache = cache; }

	// Slack in pixels along each axis for one pattern: its own fraction
	// where it named one, MIN_MARGINE where it did not.
	cv::Point search_margines(const ImagePattern& pattern) const;

	// library and capture must outlive the detector.
	// The library is not const: a successful search records where the
	// pattern was found, which is what makes the next one quick.
	bool start(
		ImageLibrary&        library,
		const ScreenCapture& capture,
		std::string&         error
	);
	void stop();

	bool     running()       const noexcept { return m_running.load(); }
	uint64_t request_count() const noexcept { return m_request_count.load(); }

	// Searches the most recent frame for library patterns and waits for the
	// answer. Returns false on error - no capture running, unknown name,
	// timeout - and fills error; result is then left alone. Finding nothing
	// is not an error: it returns true with no hits.
	//
	// Several patterns mean "any of these": they are all the same thing as
	// far as the caller is concerned, every one of the max_hits reported
	// may come from any of them, and each hit says which pattern it was.
	// This is not the same as searching for them one after another - the
	// boxes of all of them are looked at before any full frame search is,
	// so a pattern that has moved cannot cost the others their quick pass.
	//
	// A pattern with FIXED_DIRECTIONS that has been found before is first
	// looked for in a box around that position, and only then, if nothing
	// at all turned up, in the whole frame. quick_only stops after the
	// boxes - it is for a caller that would rather have a fast "no" than a
	// slow "yes", and is an error unless every pattern has a box.
	bool detect(
		const std::vector<size_t>& images,
		int                        max_hits,
		bool                       quick_only,
		Detection&                 result,
		std::string&               error,
		std::chrono::milliseconds  timeout = DEFAULT_TIMEOUT
	);
	// One pattern, the common case.
	bool detect(
		size_t                    image,
		int                       max_hits,
		bool                      quick_only,
		Detection&                result,
		std::string&              error,
		std::chrono::milliseconds timeout = DEFAULT_TIMEOUT
	);
	// Same, for names that have not been resolved yet. An unknown name is
	// the one failure the index forms cannot report, so it is spelt out
	// here rather than left to the caller.
	bool detect(
		const std::vector<std::string>& names,
		int                             max_hits,
		bool                            quick_only,
		Detection&                      result,
		std::string&                    error,
		std::chrono::milliseconds       timeout = DEFAULT_TIMEOUT
	);
	bool detect(
		const std::string&        name,
		int                       max_hits,
		bool                      quick_only,
		Detection&                result,
		std::string&              error,
		std::chrono::milliseconds timeout = DEFAULT_TIMEOUT
	);

	// "'jump'", "'jump' or 'dock'", "'a', 'b' or 'c'" - for messages about
	// a search that covered more than one pattern.
	std::string names_text(const std::vector<size_t>& images) const;

private:
	void detect_loop();
	// Runs one search. Reports trouble through error rather than throwing.
	void run_match(
		const std::vector<size_t>& images,
		int                        max_hits,
		bool                       quick_only,
		Detection&                 result,
		std::string&               error
	) const;

	// Searches one rectangle of a BGRA frame for one pattern. Hits come
	// back in frame coordinates, best match first, replacing whatever was
	// in the vector. mistaken counts the candidates a similar pattern
	// turned out to explain better.
	void search_area(
		const cv::Mat&             captured,
		const cv::Rect&            area,
		size_t                     image,
		int                        max_hits,
		std::vector<DetectionHit>& hits,
		int&                       mistaken
	) const;

	// Closest this pattern comes to any position in a window of the scene,
	// as a root mean square pixel difference on the images' own 0..1
	// scale. Normalised by the mask, so patterns of different sizes are
	// still comparable. WORST_FIT when it does not fit in the window.
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

	ImageLibrary*        m_library {nullptr};
	const ScreenCapture* m_capture {nullptr};
	PositionCache*       m_cache   {nullptr};
	int                  m_min_margine {4};

	std::atomic<bool>       m_running       {false};
	std::atomic<uint64_t>   m_request_count {0};
	std::thread             m_thread;

	std::mutex              m_mutex;
	std::condition_variable m_wake;  // worker waits here for a request
	std::condition_variable m_done;  // caller waits here for the answer
	std::mutex              m_call_mutex; // serialises callers

	// Requests are numbered so a caller that timed out cannot be handed
	// the answer to somebody else's search, or a later caller the stale
	// answer to the abandoned one.
	uint64_t m_request_serial {0};
	uint64_t m_result_serial  {0};

	std::vector<size_t> m_request_images;
	int                 m_request_max_hits {1};
	bool                m_request_quick    {false};
	bool                m_pending          {false};

	Detection   m_result;
	std::string m_result_error;
};
