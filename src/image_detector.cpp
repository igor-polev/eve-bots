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

// Stands for "could not be judged": worse than any real difference, which
// cannot exceed 1 on the images' own scale.
constexpr double WORST_FIT = 2.0;

// What OpenCV's masked TM_SQDIFF has to be divided by to become a mean.
// It multiplies the difference by the mask before squaring, so the weight
// lands in the sum squared as well.
double fit_weight(const ImagePattern& pattern)
{
	if (!pattern.masked()) {
		return static_cast<double>(pattern.width())
		     * pattern.height() * pattern.image.channels();
	}
	cv::Mat squared;
	cv::multiply(pattern.mask, pattern.mask, squared);
	const cv::Scalar total = cv::sum(squared);
	return total[0] + total[1] + total[2];
}

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
	// Nothing carried over from a previous run: those answers were about
	// another client's frames, and the timestamps would never match again
	// anyway.
	m_frame = Frame {};
	m_memo.clear();
	m_memo_frame = std::chrono::steady_clock::time_point {};
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
	// Safe only now that the thread that owns them is gone, and worth
	// doing for the frame alone: that is megabytes held for nothing.
	m_frame = Frame {};
	m_memo.clear();
	m_memo_frame = std::chrono::steady_clock::time_point {};
}

bool ImageDetector::detect(
	const std::string&        name,
	int                       max_hits,
	bool                      quick_only,
	Detection&                result,
	std::string&              error,
	std::chrono::milliseconds timeout)
{
	return detect(
		std::vector<std::string> {name},
		max_hits, quick_only, result, error, timeout
	);
}

bool ImageDetector::detect(
	const std::vector<std::string>& names,
	int                             max_hits,
	bool                            quick_only,
	Detection&                      result,
	std::string&                    error,
	std::chrono::milliseconds       timeout)
{
	if (!m_running.load()) {
		error = "detection is not running";
		return false;
	}
	std::vector<size_t> images;
	images.reserve(names.size());
	for (const std::string& name : names) {
		const size_t image = m_library->index(name);
		if (ImageLibrary::NOT_FOUND == image) {
			error = "no image called '" + name + "'; the library holds: "
			      + m_library->name_list();
			return false;
		}
		images.push_back(image);
	}
	return detect(images, max_hits, quick_only, result, error, timeout);
}

bool ImageDetector::detect(
	size_t                    image,
	int                       max_hits,
	bool                      quick_only,
	Detection&                result,
	std::string&              error,
	std::chrono::milliseconds timeout)
{
	return detect(
		std::vector<size_t> {image},
		max_hits, quick_only, result, error, timeout
	);
}

bool ImageDetector::detect(
	const std::vector<size_t>& images,
	int                        max_hits,
	bool                       quick_only,
	Detection&                 result,
	std::string&               error,
	std::chrono::milliseconds  timeout)
{
	if (!m_running.load()) {
		error = "detection is not running";
		return false;
	}
	if (images.empty()) {
		error = "no image to search for";
		return false;
	}
	if (max_hits < 1) max_hits = 1;
	const std::string what = names_text(images);

	std::lock_guard<std::mutex> caller {m_call_mutex};

	uint64_t serial {0};
	{
		std::lock_guard<std::mutex> lock {m_mutex};
		serial             = ++m_request_serial;
		m_request_images   = images;
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
		error = "detection of " + what + " timed out";
		return false;
	}
	if (m_result_serial != serial) {
		error = "detection stopped before " + what + " was searched for";
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

		const std::vector<size_t> images   = m_request_images;
		const int                 max_hits = m_request_max_hits;
		const bool                quick    = m_request_quick;
		const uint64_t            serial   = m_request_serial;
		m_pending = false;
		lock.unlock();

		Detection result;
		std::string error;
		try {
			run_match(images, max_hits, quick, result, error);
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

std::string ImageDetector::names_text(const std::vector<size_t>& images) const
{
	std::string text;
	for (size_t at = 0; at < images.size(); ++at) {
		if (at > 0) text += (at + 1 == images.size()) ? " or " : ", ";
		text += "'" + (*m_library)(images[at]).name + "'";
	}
	return text;
}

cv::Point ImageDetector::search_margines(const ImagePattern& pattern) const
{
	// A per axis fraction is measured against that axis, the shared one and
	// the default against the longer side - so a wide flat button ends up
	// with as much room above it as beside it, which is where a panel that
	// grew a line actually moved it.
	const auto pixels = [this, &pattern](double axis, int size) {
		const double fraction =
			ImagePattern::margine_given(axis)                  ? axis :
			ImagePattern::margine_given(pattern.search_margine) ?
				pattern.search_margine : SEARCH_MARGINE_DEFAULT;
		const int reference =
			ImagePattern::margine_given(axis) ? size : pattern.longest();
		return std::max(
			m_min_margine,
			static_cast<int>(std::lround(fraction * reference))
		);
	};
	return cv::Point {
		pixels(pattern.search_margine_x, pattern.width()),
		pixels(pattern.search_margine_y, pattern.height())
	};
}

double ImageDetector::best_fit(
	const cv::Mat&      scene,
	const cv::Rect&     window,
	const ImagePattern& pattern) const
{
	if (window.width  < pattern.width() ||
		window.height < pattern.height())
	{
		return WORST_FIT;
	}

	// TM_SQDIFF, not the TM_CCOEFF_NORMED the search runs on: this is the
	// step that has to see a difference in colour, and CCOEFF removes each
	// channel's mean before correlating, which is exactly what hides one.
	cv::Mat result;
	cv::matchTemplate(
		scene(window), pattern.image, result, cv::TM_SQDIFF,
		pattern.masked() ? pattern.mask : cv::noArray()
	);

	double closest {0.0};
	cv::minMaxLoc(result, &closest, nullptr, nullptr, nullptr);

	const double weight = fit_weight(pattern);
	if (weight <= 0.0) return WORST_FIT;
	// Back to the images' own scale, and comparable between patterns of
	// different sizes. Rounding can leave the sum a hair below zero.
	return std::sqrt(std::max(0.0, closest) / weight);
}

cv::Rect ImageDetector::quick_box(
	const ImagePattern& pattern,
	const cv::Point&    last,
	const cv::Rect&     frame) const
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
	size_t                     image,
	int                        max_hits,
	std::vector<DetectionHit>& hits,
	int&                       mistaken) const
{
	const ImagePattern& pattern = (*m_library)(image);

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

	// Both sides of a contest are weighed over one window, wide enough to
	// hold the largest lookalike wherever it sits: a rival a pixel or two
	// out of step, or a size or two bigger, still gets its best shot. The
	// candidate is not pinned to the corner the correlation liked, because
	// that corner is its best by a different measure.
	int slack = m_min_margine;
	for (const size_t twin : pattern.similar) {
		const ImagePattern& other = (*m_library)(twin);
		slack = std::max(slack, std::abs(other.width()  - pattern.width()));
		slack = std::max(slack, std::abs(other.height() - pattern.height()));
	}

	// Whichever lookalike claimed the last candidate is tried first on the
	// next one: on a screen full of one kind of thing, that is usually the
	// one that will claim it again, and the rest need never be measured.
	size_t first_twin = 0;

	hits.clear();
	mistaken = 0;
	// Every candidate is blanked below, accepted or not, so each pass takes
	// at least one position out of a finite map: the search ends on its own
	// once nothing is left above the threshold.
	while (static_cast<int>(hits.size()) < max_hits) {
		double    certainty {0.0};
		cv::Point at;
		cv::minMaxLoc(result, nullptr, &certainty, nullptr, &at);
		if (certainty < pattern.threshold) break;

		// The shape fits. If anything it could be mistaken for explains
		// these pixels better, it was that thing and not this one.
		double fit {0.0};
		bool   claimed {false};
		if (!pattern.similar.empty()) {
			cv::Rect window {
				at.x - slack, at.y - slack,
				pattern.width() + 2 * slack, pattern.height() + 2 * slack
			};
			window &= cv::Rect {0, 0, scene.cols, scene.rows};

			fit = best_fit(scene, window, pattern);
			for (size_t step = 0; step < pattern.similar.size(); ++step) {
				const size_t twin =
					pattern.similar[(first_twin + step) % pattern.similar.size()];
				// Stop at the first one that fits better - measuring the
				// rest cannot change the answer.
				if (best_fit(scene, window, (*m_library)(twin)) < fit) {
					first_twin = (first_twin + step) % pattern.similar.size();
					claimed = true;
					break;
				}
			}
		}
		if (claimed) {
			++mistaken;
		} else {
			// matchTemplate positions are top left corners already, so a
			// hit only moves from result into frame coordinates.
			hits.push_back(
				DetectionHit {image, at + area.tl(), certainty, fit}
			);
		}

		// Blank this match either way: a rejected one left standing would
		// simply be found again on the next pass, forever.
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

namespace {

// How long ago a frame was taken.
std::chrono::milliseconds since_taken(const Frame& frame)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - frame.taken
	);
}

} // namespace

std::chrono::milliseconds ImageDetector::frame_life() const
{
	const unsigned rate = m_capture ? m_capture->frame_rate() : 0;
	// No rate to go on means capture is not running, and nothing will be
	// answered anyway; a second is long enough to be harmless.
	if (0 == rate) return std::chrono::milliseconds {1000};
	return std::chrono::milliseconds {std::max(1u, 1000 / rate)};
}

void ImageDetector::run_match(
	const std::vector<size_t>& images,
	int                        max_hits,
	bool                       quick_only,
	Detection&                 result,
	std::string&               error)
{
	if (!m_capture->running()) {
		error = "capture is not running; use 'start' first";
		return;
	}
	// Capture hands a frame over by value, so asking for one copies every
	// pixel of it - the cost the whole of this is trying to avoid. A frame
	// younger than the interval capture runs at is still the newest there
	// can be, so it is kept and asked about again instead of replaced.
	const std::chrono::milliseconds life = frame_life();
	if (m_frame.empty() || since_taken(m_frame) >= life) {
		// A detection asked for right after 'start' would otherwise fail:
		// the window has to redraw before Graphics Capture hands over
		// anything. The wait sits on the detection thread, so nothing else
		// is held up.
		Frame newest = m_capture->latest_frame();
		for (std::chrono::milliseconds waited {0};
		     newest.empty() && waited < FIRST_FRAME_WAIT && m_running.load();
		     waited += FRAME_POLL_STEP)
		{
			std::this_thread::sleep_for(FRAME_POLL_STEP);
			newest = m_capture->latest_frame();
		}
		// Capture handing back what we already have is not a reason to
		// throw away what we know about it.
		if (!newest.empty()) m_frame = std::move(newest);
	}
	if (m_frame.empty()) {
		error = "no frame captured yet - is the window minimised?";
		return;
	}
	const Frame& frame = m_frame;

	// A frame that is not the one everything was remembered about makes
	// all of it worthless at a stroke: those answers were about pixels
	// that no longer exist.
	if (frame.taken != m_memo_frame) {
		m_memo.clear();
		m_memo_frame = frame.taken;
	}
	// Whether this frame can still be spoken for is settled once, here, so
	// that a search long enough to outlive the frame it ran on does not
	// then refuse to record what it found. The window is wider than the
	// one above on purpose: that one asks "is there likely to be a newer
	// frame", this one asks "has capture stopped producing them at all",
	// and only the second is a reason to distrust what we already know.
	const bool fresh = since_taken(frame) < MEMO_FRAME_LIVES * life;

	if (fresh) {
		for (const Remembered& memo : m_memo) {
			if (!memo.answers(images, max_hits, quick_only)) continue;
			// The same question, the same pixels. Searching again would
			// spend anything up to two seconds arriving back here.
			result = memo.answer;
			result.remembered = true;
			return;
		}
	}

	for (const size_t image : images) {
		const ImagePattern& pattern = (*m_library)(image);
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
	}

	// The frame is our own copy, so wrapping it costs nothing; the cast is
	// only needed because cv::Mat has no const-pointer constructor.
	const cv::Mat captured {
		static_cast<int>(frame.height),
		static_cast<int>(frame.width),
		CV_8UC4,
		const_cast<uint8_t*>(frame.pixels.data()),
		static_cast<size_t>(frame.stride())
	};
	const cv::Rect whole {
		0, 0, static_cast<int>(frame.width), static_cast<int>(frame.height)
	};

	// A box is only worth searching when the pattern is known to stay put
	// along at least one axis and has been seen there at least once. Each
	// pattern answers that for itself, so a search for several of them can
	// be quick for some and not for others.
	result = Detection {};
	result.searched.reserve(images.size());
	std::string no_box;
	for (const size_t image : images) {
		const ImagePattern& pattern = (*m_library)(image);
		const cv::Point     last    = m_library->last_hit(image);

		PatternSearch part;
		part.image = image;
		if (ImageLibrary::boxable(pattern, last)) {
			part.scope = SearchScope::BOX;
			part.box   = quick_box(pattern, last, whole);
		} else if (quick_only) {
			// Skipping it would answer a question nobody asked - "is it in
			// the boxes of the others" - and look like a complete answer.
			if (!no_box.empty()) no_box += ", ";
			no_box += "'" + pattern.name + "' "
			        + (FIXED_NONE == pattern.fixed_directions
			           ? "has no FIXED_DIRECTIONS in "
			             + to_utf8(ImageLibrary::FILE_NAME)
			           : std::string("has not been found yet"));
		}
		result.searched.push_back(part);
	}
	if (!no_box.empty()) {
		error = "no quick search: " + no_box;
		result = Detection {};
		return;
	}

	// One pattern's share of the work, wherever it is being looked for.
	std::vector<DetectionHit> found;
	const auto look = [&](PatternSearch& part, const cv::Rect& area) {
		int mistaken {0};
		search_area(captured, area, part.image, max_hits, found, mistaken);
		part.mistaken += mistaken;
		if (found.empty()) return;

		// Remembering where it went is what makes the next search quick.
		// The best match leads, so found.front() is the one to keep. A
		// position that has not moved is not offered to the cache at all,
		// which is most of them once the client has settled.
		const cv::Point corner = found.front().at;
		if (m_library->set_last_hit(part.image, corner) && m_cache)
			m_cache->store((*m_library)(part.image), corner);

		result.hits.insert(result.hits.end(), found.begin(), found.end());
	};

	// Every box before any full frame: one pattern that has moved must not
	// cost the others their quick pass.
	for (PatternSearch& part : result.searched)
		if (SearchScope::BOX == part.scope) look(part, part.box);

	// Nothing anywhere means the caller gets the slow answer it asked for -
	// for all of the patterns, since any of them would do. A box that did
	// answer spares the rest that cost: one of them was all that was asked
	// for, and the ones left at NONE say plainly they were never looked at.
	if (result.hits.empty() && !quick_only) {
		for (PatternSearch& part : result.searched) {
			part.scope = SearchScope::BOX == part.scope
				? SearchScope::BOX_THEN_FULL : SearchScope::FULL;
			look(part, whole);
		}
	}

	// Best first, whichever pattern it came from, and never more than were
	// asked for: each pattern may have contributed up to max_hits.
	std::stable_sort(
		result.hits.begin(), result.hits.end(),
		[](const DetectionHit& one, const DetectionHit& other) {
			return one.certainty > other.certainty;
		}
	);
	if (static_cast<int>(result.hits.size()) > max_hits)
		result.hits.resize(static_cast<size_t>(max_hits));

	// Kept whole rather than picked apart per pattern: handing back what
	// this search actually produced is exact, where rebuilding an answer
	// out of remembered pieces would be a second implementation of the
	// search to keep in step with this one.
	if (fresh && m_memo.size() < MEMO_MAX)
		m_memo.push_back(Remembered {images, max_hits, quick_only, result});
}
