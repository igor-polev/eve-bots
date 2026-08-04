/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector implementation.
*/

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "image_detector.hpp"
#include "text_util.hpp"

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

// Span of a quick search along one axis: the pattern where it was last
// seen, widened by margin and clipped to the frame. Clipping never makes
// the span narrower than the pattern, which would leave nothing to match.
void quick_span(
	int at, int size, int margin, int limit, int& start, int& length)
{
	start   = std::max(0, at - margin);
	int end = std::min(limit, at + size + margin);
	if (end - start < size) {
		if (end >= limit) start = std::max(0, limit - size);
		end = std::min(limit, start + size);
	}
	length = end - start;
}

} // namespace

ImageDetector::~ImageDetector()
{
	stop();
}

bool ImageDetector::start(
	ImageLibrary& library, const ScreenCapture& capture,
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
	const std::string&        name,
	int                       max_hits,
	bool                      quick_only,
	Detection&                result,
	std::string&              error,
	std::chrono::milliseconds timeout)
{
	if (!m_running.load()) {
		error = "detection is not running";
		return false;
	}
	const size_t image = m_library->index(name);
	if (ImageLibrary::NOT_FOUND == image) {
		error = "no image called '" + name + "'; the library holds: "
		      + m_library->name_list();
		return false;
	}
	return detect(image, max_hits, quick_only, result, error, timeout);
}

bool ImageDetector::detect(
	size_t                    image,
	int                       max_hits,
	bool                      quick_only,
	Detection&                result,
	std::string&              error,
	std::chrono::milliseconds timeout)
{
	if (!m_running.load()) {
		error = "detection is not running";
		return false;
	}
	if (max_hits < 1) max_hits = 1;
	const std::string& name = (*m_library)(image).name;

	std::lock_guard<std::mutex> caller {m_call_mutex};

	uint64_t serial {0};
	{
		std::lock_guard<std::mutex> lock {m_mutex};
		serial             = ++m_request_serial;
		m_request_image    = image;
		m_request_max_hits = max_hits;
		m_request_quick    = quick_only;
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
	result = m_result;
	return true;
}

void ImageDetector::detect_loop()
{
	std::unique_lock<std::mutex> lock {m_mutex};
	while (m_running.load()) {
		m_wake.wait(lock, [this] { return m_pending || !m_running.load(); });
		if (!m_running.load()) break;

		const size_t   image    = m_request_image;
		const int      max_hits = m_request_max_hits;
		const bool     quick    = m_request_quick;
		const uint64_t serial   = m_request_serial;
		m_pending = false;
		lock.unlock();

		Detection result;
		std::string error;
		try {
			run_match(image, max_hits, quick, result, error);
		}
		catch (const cv::Exception& e) {
			error = std::string("image matching failed: ") + e.what();
		}
		catch (const std::exception& e) {
			error = std::string("image matching failed: ") + e.what();
		}
		m_request_count.fetch_add(1);

		lock.lock();
		m_result       = std::move(result);
		m_result_error = std::move(error);
		m_result_serial = serial;
		m_done.notify_all();
	}
	// let go of anyone still waiting on a detector that is shutting down
	m_done.notify_all();
}

cv::Point ImageDetector::search_margines(const ImagePattern& pattern)
{
	// A per axis fraction is measured against that axis, the shared one and
	// the default against the longer side - so a wide flat button ends up
	// with as much room above it as beside it, which is where a panel that
	// grew a line actually moved it.
	const auto pixels = [&pattern](double axis, int size) {
		const double fraction =
			ImagePattern::margine_given(axis)                  ? axis :
			ImagePattern::margine_given(pattern.search_margine) ?
				pattern.search_margine : SEARCH_MARGINE_DEFAULT;
		const int reference =
			ImagePattern::margine_given(axis) ? size : pattern.longest();
		return std::max(
			SEARCH_MARGINE_MIN,
			static_cast<int>(std::lround(fraction * reference))
		);
	};
	return cv::Point {
		pixels(pattern.search_margine_x, pattern.width()),
		pixels(pattern.search_margine_y, pattern.height())
	};
}

cv::Rect ImageDetector::quick_box(
	const ImagePattern& pattern,
	const cv::Point&    last,
	const cv::Rect&     frame)
{
	const cv::Point margines = search_margines(pattern);

	cv::Rect box {frame};
	if (pattern.fixed_x()) {
		quick_span(
			last.x, pattern.width(), margines.x,
			frame.width, box.x, box.width
		);
	}
	if (pattern.fixed_y()) {
		quick_span(
			last.y, pattern.height(), margines.y,
			frame.height, box.y, box.height
		);
	}
	return box;
}

void ImageDetector::search_area(
	const cv::Mat&             captured,
	const cv::Rect&            area,
	const ImagePattern&        pattern,
	int                        max_hits,
	std::vector<DetectionHit>& hits)
{
	// Converting only the searched rectangle is what makes a quick search
	// cheap: on an ultrawide frame the conversion alone costs more than
	// matching a small box does.
	cv::Mat colour;
	cv::cvtColor(captured(area), colour, cv::COLOR_BGRA2BGR);
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

	const int pad_x = pattern.width()  / SUPPRESSION_DIVISOR;
	const int pad_y = pattern.height() / SUPPRESSION_DIVISOR;

	hits.clear();
	for (int found = 0; found < max_hits; ++found) {
		double    certainty {0.0};
		cv::Point at;
		cv::minMaxLoc(result, nullptr, &certainty, nullptr, &at);
		if (certainty < pattern.threshold) break;

		// matchTemplate positions are top left corners already, so a hit
		// only has to move from result coordinates into frame coordinates
		hits.push_back(DetectionHit {at + area.tl(), certainty});
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

void ImageDetector::run_match(
	size_t       image,
	int          max_hits,
	bool         quick_only,
	Detection&   result,
	std::string& error) const
{
	const ImagePattern& pattern = (*m_library)(image);

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
	const cv::Rect whole {
		0, 0, static_cast<int>(frame.width), static_cast<int>(frame.height)
	};

	// A box is only worth searching when the pattern is known to stay put
	// along at least one axis and has been seen at least once.
	const cv::Point last  = m_library->last_hit(image);
	const bool      boxed = FIXED_NONE != pattern.fixed_directions
	                     && ImageLibrary::seen(last);
	if (quick_only && !boxed) {
		error = "no quick search for '" + pattern.name + "': it "
		      + (FIXED_NONE == pattern.fixed_directions
		         ? "has no FIXED_DIRECTIONS in " + to_utf8(ImageLibrary::FILE_NAME)
		         : std::string("has not been found yet"));
		return;
	}

	result = Detection {};
	if (boxed) {
		result.box   = quick_box(pattern, last, whole);
		result.scope = SearchScope::BOX;
		search_area(captured, result.box, pattern, max_hits, result.hits);
	}
	if (result.hits.empty() && !quick_only) {
		result.scope = boxed ? SearchScope::BOX_THEN_FULL : SearchScope::FULL;
		search_area(captured, whole, pattern, max_hits, result.hits);
	}

	// Remembering where it went is what makes the next search quick. The
	// best match leads, so hits.front() is the one to keep.
	if (!result.hits.empty())
		m_library->set_last_hit(image, result.hits.front().at);
}
