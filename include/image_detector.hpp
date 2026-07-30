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
	int    x {0};             // centre of the match, in frame pixels
	int    y {0};
	double certainty {0.0};   // 0..1, how well the pattern matched
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

	// library and capture must outlive the detector.
	bool start(
		const ImageLibrary&  library,
		const ScreenCapture& capture,
		std::string&         error
	);
	void stop();

	bool     running()       const noexcept { return m_running.load(); }
	uint64_t request_count() const noexcept { return m_request_count.load(); }

	// Searches the most recent frame for the named pattern and waits for
	// the answer. Returns false on error - no capture running, unknown
	// name, timeout - and fills error; hits is then left alone.
	// Finding nothing is not an error: it returns true with hits empty.
	bool detect(
		const std::string&        name,
		int                       max_hits,
		std::vector<DetectionHit>& hits,
		std::string&              error,
		std::chrono::milliseconds timeout = DEFAULT_TIMEOUT
	);

private:
	void detect_loop();
	// Runs one search. Reports trouble through error rather than throwing.
	void run_match(
		const ImagePattern&        pattern,
		int                        max_hits,
		std::vector<DetectionHit>& hits,
		std::string&               error
	) const;

	const ImageLibrary*  m_library {nullptr};
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

	const ImagePattern* m_request_pattern  {nullptr};
	int                 m_request_max_hits {1};
	bool                m_pending          {false};

	std::vector<DetectionHit> m_result_hits;
	std::string               m_result_error;
};
