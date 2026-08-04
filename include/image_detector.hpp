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
#include "screen_capture.hpp"

struct DetectionHit {
	cv::Point at;             // top left corner of the match, in frame pixels
	double    certainty {0.0};   // 0..1, how well the pattern matched
};

// Which part of the frame a search covered. Reported back because the two
// differ by two orders of magnitude in cost, so a caller wondering why a
// detection was slow needs to see which one it got.
enum class SearchScope {
	FULL,          // the whole frame
	BOX,           // only the box around the remembered position
	BOX_THEN_FULL  // the box missed, so the whole frame followed
};

struct Detection {
	std::vector<DetectionHit> hits;
	SearchScope scope {SearchScope::FULL};
	cv::Rect    box;   // the quick box, empty unless scope mentions one

	bool quick() const noexcept { return SearchScope::BOX == scope; }
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

	// Slack in pixels along each axis for one pattern: its own fraction
	// where it named one, this class's default where it did not.
	static cv::Point search_margines(const ImagePattern& pattern);

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

	// Searches the most recent frame for one library pattern and waits for
	// the answer. Returns false on error - no capture running, unknown
	// name, timeout - and fills error; result is then left alone.
	// Finding nothing is not an error: it returns true with no hits.
	//
	// A pattern with FIXED_DIRECTIONS that has been found before is first
	// looked for in a box around that position, and only then, if that
	// missed, in the whole frame. quick_only stops after the box - it is
	// for a caller that would rather have a fast "no" than a slow "yes",
	// and is an error when there is no box to search.
	bool detect(
		size_t                    image,
		int                       max_hits,
		bool                      quick_only,
		Detection&                result,
		std::string&              error,
		std::chrono::milliseconds timeout = DEFAULT_TIMEOUT
	);
	// Same, for a name that has not been resolved yet. An unknown name is
	// the one failure the index form cannot report, so it is spelt out
	// here rather than left to the caller.
	bool detect(
		const std::string&        name,
		int                       max_hits,
		bool                      quick_only,
		Detection&                result,
		std::string&              error,
		std::chrono::milliseconds timeout = DEFAULT_TIMEOUT
	);

private:
	void detect_loop();
	// Runs one search. Reports trouble through error rather than throwing.
	void run_match(
		size_t       image,
		int          max_hits,
		bool         quick_only,
		Detection&   result,
		std::string& error
	) const;

	// Searches one rectangle of a BGRA frame. Hits come back in frame
	// coordinates, best match first.
	static void search_area(
		const cv::Mat&             captured,
		const cv::Rect&            area,
		const ImagePattern&        pattern,
		int                        max_hits,
		std::vector<DetectionHit>& hits
	);

	// The box a quick search covers: the pattern at its remembered corner,
	// grown by a margin along every fixed direction and spanning the whole
	// frame along the others.
	static cv::Rect quick_box(
		const ImagePattern& pattern,
		const cv::Point&    last,
		const cv::Rect&     frame
	);

	ImageLibrary*        m_library {nullptr};
	const ScreenCapture* m_capture {nullptr};

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

	size_t m_request_image    {0};
	int    m_request_max_hits {1};
	bool   m_request_quick    {false};
	bool   m_pending          {false};

	Detection   m_result;
	std::string m_result_error;
};
