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

// Span of a quick search along one axis. Defined at the end of the file.
void quick_span(int at, int size, int margin, int limit, int& start, int& length);

} // namespace

ImageDetector::ImageDetector(
	ImageLibrary& library, ScreenCapture& capture, PositionCache& cache)
	: m_library {library}, m_capture {capture}, m_cache {cache}
{
}

bool ImageDetector::detect(
	const eb::Images& images,
	Detection&        result,
	std::string&      error,
	SearchScope       scope,
	int               max_hits)
{
	std::lock_guard<std::mutex> lock {m_mutex};

	result = Detection {};
	if (images.empty()) {
		error = "no image to search for";
		return false;
	}
	if (SearchScope::NONE == scope) {
		error = "no search scope to look in";
		return false;
	}
	if (max_hits < 1) max_hits = 1;
	if (!m_capture.running()) {
		error = "capture is not running; use 'start' first";
		return false;
	}

	// A frame we have not been handed before makes everything remembered
	// worthless at a stroke: those answers were about pixels that no longer
	// exist. Capture leaves the frame alone while it is still the newest
	// there can be, and then the memo stands.
	if (m_capture.frame(m_frame)) m_memo.clear();
	if (m_frame.empty()) {
		error = "no frame captured yet - is the window minimised?";
		return false;
	}
	for (const size_t image : images) {
		const ImagePattern& pattern = m_library(image);
		if (pattern.width()  > static_cast<int>(m_frame.width) ||
			pattern.height() > static_cast<int>(m_frame.height))
		{
			error = "pattern '" + pattern.name + "' is "
			      + std::to_string(pattern.width()) + "x"
			      + std::to_string(pattern.height())
			      + ", larger than the "
			      + std::to_string(m_frame.width) + "x"
			      + std::to_string(m_frame.height) + " frame";
			return false;
		}
	}

	// What is already known about this frame, pattern by pattern. One that
	// was found answers the whole request, any of them being all that was
	// asked for; one that was searched and missed need not be searched again.
	result.scope = scope;
	result.searched.assign(images.size(), PatternSearch {});
	std::vector<const Remembered*> known(images.size(), nullptr);
	bool answered {false};
	for (size_t at = 0; at < images.size(); ++at) {
		result.searched[at].image = images[at];
		for (const Remembered& memo : m_memo) {
			if (!memo.answers(images[at], max_hits, scope)) continue;
			known[at]             = &memo;
			result.searched[at]   = memo.searched;
			answered              = answered || !memo.hits.empty();
			break;
		}
	}
	if (answered) {
		for (size_t at = 0; at < images.size(); ++at) {
			if (!known[at]) continue;
			result.hits.insert(
				result.hits.end(),
				known[at]->hits.begin(), known[at]->hits.end()
			);
		}
		result.remembered = true;
		rank_hits(result.hits, max_hits);
		return true;
	}

	// The frame is our own copy, so wrapping it costs nothing; the cast is
	// only needed because cv::Mat has no const-pointer constructor.
	const cv::Mat captured {
		static_cast<int>(m_frame.height),
		static_cast<int>(m_frame.width),
		CV_8UC4,
		const_cast<uint8_t*>(m_frame.pixels.data()),
		static_cast<size_t>(m_frame.stride())
	};
	const cv::Rect whole {
		0, 0, static_cast<int>(m_frame.width), static_cast<int>(m_frame.height)
	};

	// Hits per pattern, so each can be remembered on its own account. The
	// answer merges them; the memo does not.
	std::vector<std::vector<DetectionHit>> mine(images.size());
	try {
		// A box is only worth searching when the pattern is known to stay put
		// along at least one axis and has been seen there at least once. Each
		// pattern answers that for itself.
		std::string no_box;
		for (size_t at = 0; at < images.size(); ++at) {
			if (known[at]) continue;   // searched already, and not there

			const ImagePattern& pattern = m_library(images[at]);
			const cv::Point     last    = m_library.last_hit(images[at]);
			if (SearchScope::FULL != scope
				&& ImageLibrary::boxable(pattern, last))
			{
				result.searched[at].scope = SearchScope::BOX;
				result.searched[at].box   = quick_box(pattern, last, whole);
			}
			else if (SearchScope::BOX == scope) {
				// Skipping it would answer a question nobody asked - "is it
				// in the boxes of the others" - and look like a complete
				// answer.
				if (!no_box.empty()) no_box += ", ";
				no_box += "'" + pattern.name + "' "
				        + (FIXED_NONE == pattern.fixed_directions
				           ? "has no FIXED_DIRECTIONS in "
				             + to_utf8(ImageLibrary::FILE_NAME)
				           : std::string("has not been found yet"));
			}
		}
		if (!no_box.empty()) {
			error  = "no quick search: " + no_box;
			result = Detection {};
			return false;
		}

		// One pattern's share of the work, wherever it is being looked for.
		const auto look = [&](size_t at, const cv::Rect& area) {
			PatternSearch& part = result.searched[at];
			int mistaken {0};
			search_area(captured, area, part.image, max_hits, mine[at], mistaken);
			part.mistaken += mistaken;
			if (mine[at].empty()) return;

			for (DetectionHit& hit : mine[at]) hit.scope = scope;
			// Remembering where it went is what makes the next search quick.
			// A position that has not moved is not offered to the cache.
			const cv::Point corner = mine[at].front().at;
			if (m_library.set_last_hit(part.image, corner))
				m_cache.store(m_library(part.image), corner);
		};

		// Every box before any full frame: one pattern that has moved must
		// not cost the others their quick pass.
		bool any {false};
		for (size_t at = 0; at < images.size(); ++at) {
			// A remembered part carries the box of the search that made it,
			// which is not an invitation to search it over again.
			if (known[at]) continue;
			if (SearchScope::BOX != result.searched[at].scope) continue;
			look(at, result.searched[at].box);
			any = any || !mine[at].empty();
		}

		// Nothing anywhere means the caller gets the slow answer it asked
		// for, for all of the patterns, since any of them would do. A box
		// that did answer spares the rest that cost, and the ones left at
		// NONE say plainly they were never looked at.
		if (!any && SearchScope::BOX != scope) {
			for (size_t at = 0; at < images.size(); ++at) {
				if (known[at]) continue;
				result.searched[at].scope =
					SearchScope::BOX == result.searched[at].scope
						? SearchScope::BOX_THEN_FULL : SearchScope::FULL;
				look(at, whole);
			}
		}
	}
	catch (const cv::Exception& e) {
		error  = std::string("image matching failed: ") + e.what();
		result = Detection {};
		return false;
	}

	// Each pattern on its own account, so that a later request naming it
	// among others finds it here whatever else it is asked alongside.
	for (size_t at = 0; at < images.size(); ++at) {
		if (known[at]) continue;
		result.hits.insert(
			result.hits.end(), mine[at].begin(), mine[at].end()
		);
		// One never looked at settles nothing: another pattern answered
		// before this one cost anything.
		if (SearchScope::NONE == result.searched[at].scope) continue;
		// A miss settles only the ground that was covered; a hit settles
		// the question that was asked.
		m_memo.push_back(Remembered {
			images[at],
			max_hits,
			mine[at].empty() ? result.searched[at].scope : scope,
			result.searched[at],
			mine[at]
		});
	}
	rank_hits(result.hits, max_hits);
	return true;
}

void ImageDetector::rank_hits(std::vector<DetectionHit>& hits, int max_hits)
{
	// Best first, whichever pattern it came from, and never more than were
	// asked for: each pattern may have contributed up to max_hits.
	std::stable_sort(
		hits.begin(), hits.end(),
		[](const DetectionHit& one, const DetectionHit& other) {
			return one.certainty > other.certainty;
		}
	);
	if (static_cast<int>(hits.size()) > max_hits)
		hits.resize(static_cast<size_t>(max_hits));
}

void ImageDetector::search_area(
	const cv::Mat&             captured,
	const cv::Rect&            area,
	size_t                     image,
	int                        max_hits,
	std::vector<DetectionHit>& hits,
	int&                       mistaken) const
{
	const ImagePattern& pattern = m_library(image);

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

	// Normalised correlation divides by the variance under the mask, which is
	// zero wherever both frame and pattern are flat - those positions come
	// back as NaN or infinity and would otherwise win every search.
	cv::patchNaNs(result, 0.0f);
	cv::threshold(
		result, result, MAX_VALID_CERTAINTY, 0.0, cv::THRESH_TOZERO_INV
	);

	const int pad_x = pattern.width()  / SUPPRESSION_DIVISOR;
	const int pad_y = pattern.height() / SUPPRESSION_DIVISOR;

	// Both sides of a contest are weighed over one window, wide enough to
	// hold the largest lookalike wherever it sits: a rival a pixel or two out
	// of step, or a size or two bigger, still gets its best shot. The
	// candidate is not pinned to the corner the correlation liked, because
	// that corner is its best by a different measure.
	int slack = MIN_MARGINE;
	for (const size_t twin : pattern.similar) {
		const ImagePattern& other = m_library(twin);
		slack = std::max(slack, std::abs(other.width()  - pattern.width()));
		slack = std::max(slack, std::abs(other.height() - pattern.height()));
	}

	// Whichever lookalike claimed the last candidate is tried first on the
	// next one: on a screen full of one kind of thing, that is usually the
	// one that will claim it again, and the rest need never be measured.
	size_t first_twin = 0;

	hits.clear();
	mistaken = 0;
	// Every candidate is blanked below, accepted or not, so each pass takes at
	// least one position out of a finite map: the search ends on its own once
	// nothing is left above the threshold.
	while (static_cast<int>(hits.size()) < max_hits) {
		double    certainty {0.0};
		cv::Point at;
		cv::minMaxLoc(result, nullptr, &certainty, nullptr, &at);
		if (certainty < pattern.threshold) break;

		// The shape fits. If anything it could be mistaken for explains these
		// pixels better, it was that thing and not this one.
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
				// Stop at the first one that fits better - measuring the rest
				// cannot change the answer.
				if (best_fit(scene, window, m_library(twin)) < fit) {
					first_twin = (first_twin + step) % pattern.similar.size();
					claimed = true;
					break;
				}
			}
		}
		if (claimed) {
			++mistaken;
		} else {
			// matchTemplate positions are top left corners already, so a hit
			// only moves from result into frame coordinates.
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

	// What OpenCV's masked TM_SQDIFF has to be divided by to become a mean:
	// it multiplies the difference by the mask before squaring, so the weight
	// lands in the sum squared as well.
	double weight {0.0};
	if (!pattern.masked()) {
		weight = static_cast<double>(pattern.width())
		       * pattern.height() * pattern.image.channels();
	} else {
		cv::Mat squared;
		cv::multiply(pattern.mask, pattern.mask, squared);
		const cv::Scalar total = cv::sum(squared);
		weight = total[0] + total[1] + total[2];
	}
	if (weight <= 0.0) return WORST_FIT;

	// Back to the images' own scale, and comparable between patterns of
	// different sizes. Rounding can leave the sum a hair below zero.
	return std::sqrt(std::max(0.0, closest) / weight);
}

cv::Point ImageDetector::search_margines(const ImagePattern& pattern) const
{
	// A per axis fraction is measured against that axis, the shared one and
	// the default against the longer side - so a wide flat button ends up with
	// as much room above it as beside it, which is where a panel that grew a
	// line actually moved it.
	const auto pixels = [this, &pattern](double axis, int size) {
		const double fraction =
			ImagePattern::margine_given(axis)                   ? axis :
			ImagePattern::margine_given(pattern.search_margine) ?
				pattern.search_margine : SEARCH_MARGINE_DEFAULT;
		const int reference =
			ImagePattern::margine_given(axis) ? size : pattern.longest();
		return std::max(
			MIN_MARGINE,
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

namespace {

// The pattern where it was last seen, widened by margin and clipped to the
// frame. Clipping never makes the span narrower than the pattern, which would
// leave nothing to match.
void quick_span(int at, int size, int margin, int limit, int& start, int& length)
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
