/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector implementation.
*/

#include <algorithm>

#include <opencv2/imgproc.hpp>

#include "image_detector.hpp"

namespace {

// Correlation values above this are impossible and mean the arithmetic
// broke down; see the comment in run_match().
constexpr double MAX_VALID_CERTAINTY = 1.001;

// Fraction of the pattern size blanked around a hit before looking for
// the next one, so a second match cannot be the same object shifted by
// a pixel.
constexpr int SUPPRESSION_DIVISOR = 3;

// How long to wait for the very first frame after capture starts.
constexpr std::chrono::milliseconds FIRST_FRAME_WAIT {2000};
constexpr std::chrono::milliseconds FRAME_POLL_STEP  {25};

} // namespace

ImageDetector::~ImageDetector()
{
	stop();
}

bool ImageDetector::start(
	const ImageLibrary& library, const ScreenCapture& capture,
	std::string& error)
{
	if (m_running.load()) {
		error = "detection is already running";
		return false;
	}
	if (library.empty()) {
		error = "image library is empty";
		return false;
	}

	m_library = &library;
	m_capture = &capture;
	m_running.store(true);
	try {
		m_thread = std::thread {&ImageDetector::detect_loop, this};
	}
	catch (const std::exception& e) {
		m_running.store(false);
		error = std::string("cannot start the detection thread: ") + e.what();
		return false;
	}
	return true;
}

void ImageDetector::stop()
{
	{
		// under the lock, so a worker about to wait cannot miss this
		std::lock_guard<std::mutex> lock {m_mutex};
		if (!m_running.load()) return;
		m_running.store(false);
	}
	m_wake.notify_all();
	m_done.notify_all();
	if (m_thread.joinable()) m_thread.join();
}

bool ImageDetector::detect(
	const std::string&         name,
	int                        max_hits,
	std::vector<DetectionHit>& hits,
	std::string&               error,
	std::chrono::milliseconds  timeout)
{
	if (!m_running.load()) {
		error = "detection is not running";
		return false;
	}
	const ImagePattern* pattern = m_library->find(name);
	if (!pattern) {
		error = "no image called '" + name + "'; the library holds: "
		      + m_library->name_list();
		return false;
	}
	if (max_hits < 1) max_hits = 1;

	std::lock_guard<std::mutex> caller {m_call_mutex};

	uint64_t serial {0};
	{
		std::lock_guard<std::mutex> lock {m_mutex};
		serial             = ++m_request_serial;
		m_request_pattern  = pattern;
		m_request_max_hits = max_hits;
		m_pending          = true;
	}
	m_wake.notify_one();

	std::unique_lock<std::mutex> lock {m_mutex};
	const bool answered = m_done.wait_for(lock, timeout, [this, serial] {
		return m_result_serial == serial || !m_running.load();
	});
	if (!answered) {
		error = "detection of '" + name + "' timed out";
		return false;
	}
	if (m_result_serial != serial) {
		error = "detection stopped before '" + name + "' was searched for";
		return false;
	}
	if (!m_result_error.empty()) {
		error = m_result_error;
		return false;
	}
	hits = m_result_hits;
	return true;
}

void ImageDetector::detect_loop()
{
	std::unique_lock<std::mutex> lock {m_mutex};
	while (m_running.load()) {
		m_wake.wait(lock, [this] { return m_pending || !m_running.load(); });
		if (!m_running.load()) break;

		const ImagePattern* pattern  = m_request_pattern;
		const int           max_hits = m_request_max_hits;
		const uint64_t      serial   = m_request_serial;
		m_pending = false;
		lock.unlock();

		std::vector<DetectionHit> hits;
		std::string error;
		try {
			run_match(*pattern, max_hits, hits, error);
		}
		catch (const cv::Exception& e) {
			error = std::string("image matching failed: ") + e.what();
		}
		catch (const std::exception& e) {
			error = std::string("image matching failed: ") + e.what();
		}
		m_request_count.fetch_add(1);

		lock.lock();
		m_result_hits  = std::move(hits);
		m_result_error = std::move(error);
		m_result_serial = serial;
		m_done.notify_all();
	}
	// let go of anyone still waiting on a detector that is shutting down
	m_done.notify_all();
}

void ImageDetector::run_match(
	const ImagePattern&        pattern,
	int                        max_hits,
	std::vector<DetectionHit>& hits,
	std::string&               error) const
{
	if (!m_capture->running()) {
		error = "capture is not running; use 'start' first";
		return;
	}
	// A detection asked for right after 'start' would otherwise fail: the
	// window has to redraw before Graphics Capture hands over anything.
	// The wait sits on the detection thread, so nothing else is held up.
	Frame frame = m_capture->latest_frame();
	for (std::chrono::milliseconds waited {0};
	     frame.empty() && waited < FIRST_FRAME_WAIT && m_running.load();
	     waited += FRAME_POLL_STEP)
	{
		std::this_thread::sleep_for(FRAME_POLL_STEP);
		frame = m_capture->latest_frame();
	}
	if (frame.empty()) {
		error = "no frame captured yet - is the window minimised?";
		return;
	}
	if (pattern.width()  > static_cast<int>(frame.width) ||
		pattern.height() > static_cast<int>(frame.height))
	{
		error = "pattern '" + pattern.name + "' is "
		      + std::to_string(pattern.width()) + "x"
		      + std::to_string(pattern.height())
		      + ", larger than the "
		      + std::to_string(frame.width) + "x"
		      + std::to_string(frame.height) + " frame";
		return;
	}

	// The frame is our own copy, so wrapping it costs nothing; the cast is
	// only needed because cv::Mat has no const-pointer constructor.
	const cv::Mat captured {
		static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC4,
		const_cast<uint8_t*>(frame.pixels.data()),
		static_cast<size_t>(frame.stride())
	};
	cv::Mat colour;
	cv::cvtColor(captured, colour, cv::COLOR_BGRA2BGR);
	cv::Mat scene;
	colour.convertTo(scene, CV_32FC3, 1.0 / 255.0);

	cv::Mat result;
	cv::matchTemplate(
		scene, pattern.image, result, cv::TM_CCOEFF_NORMED,
		pattern.masked() ? pattern.mask : cv::noArray()
	);

	// Normalised correlation divides by the variance under the mask, which
	// is zero wherever both frame and pattern are flat - those positions
	// come back as NaN or infinity and would otherwise win every search.
	cv::patchNaNs(result, 0.0f);
	cv::threshold(
		result, result, MAX_VALID_CERTAINTY, 0.0, cv::THRESH_TOZERO_INV
	);

	const int centre_x = pattern.width()  / 2;
	const int centre_y = pattern.height() / 2;
	const int pad_x    = pattern.width()  / SUPPRESSION_DIVISOR;
	const int pad_y    = pattern.height() / SUPPRESSION_DIVISOR;

	hits.clear();
	for (int found = 0; found < max_hits; ++found) {
		double    certainty {0.0};
		cv::Point at;
		cv::minMaxLoc(result, nullptr, &certainty, nullptr, &at);
		if (certainty < pattern.threshold) break;

		hits.push_back(DetectionHit {
			at.x + centre_x, at.y + centre_y, certainty
		});
		if (found + 1 >= max_hits) break;

		// blank this match so the next pass has to find a different one
		const int left   = std::max(0, at.x - pad_x);
		const int top    = std::max(0, at.y - pad_y);
		const int right  = std::min(
			result.cols, at.x + pattern.width()  + pad_x
		);
		const int bottom = std::min(
			result.rows, at.y + pattern.height() + pad_y
		);
		result(cv::Rect(left, top, right - left, bottom - top)) =
			cv::Scalar(0.0);
	}
}
