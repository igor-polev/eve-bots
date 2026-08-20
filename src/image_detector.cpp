/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageDetector implementation.
*/

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "image_detector.hpp"

bool ImageDetector::detect(
	const eb::Images& images,
	Detection&        result,
	std::string&      error,
	eb::Scope         scope,
	int               max_hits)
{
	// FUTURE: optimize multi-thread sync for multiple EVE clients monitoring
	std::lock_guard<std::mutex> lock {m_mutex};

	if (images.empty()) {
		error = "no image to search for";
		return false;
	}
	if (eb::Scope::NONE == scope) {
		error = "no search scope to look in";
		return false;
	}
	if (max_hits < 1) max_hits = 1;
	if (!m_capture.running()) {
		error = "capture is not running; use 'start' first";
		return false;
	}

	// A frame we have not seen before makes every kept answer useless, and
	// every converted pixel with it: both were about pixels that are gone.
	// While capture hands back the same frame, the kept answers still hold.
	if (m_capture.new_frame(m_frame)) {
		m_memo.clear();
		m_converted = cv::Rect {};
	}
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

	// The frame is our own copy, so wrapping it costs nothing. The cast is
	// needed only because cv::Mat has no constructor for a const pointer.
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

	// Room for the float copy of the frame. create() allocates only when the
	// window changed size, which is rare, and a size change always arrives as
	// a new frame, so m_converted has just been cleared above.
	m_scene.create(captured.rows, captured.cols, CV_32FC3);

	// Where a quick search would look, worked out for every pattern first.
	// It is both what the box pass searches and what a box request cuts a
	// kept answer down to. A box is only possible when the pattern stays in
	// place along at least one axis and has been seen there once. Each
	// pattern answers that for itself.
	result = Detection {};
	result.searched.assign(images.size(), PatternSearch {});
	for (size_t at = 0; at < images.size(); ++at) {
		const ImagePattern& pattern  = m_library(images[at]);
		const cv::Point     last_hit = m_library.last_hit(images[at]);

		result.searched[at].image = images[at];
		if (eb::Scope::FULL != scope && ImageLibrary::boxable(pattern, last_hit)) {
			result.searched[at].scope = eb::Scope::BOX;
			result.searched[at].box   = quick_box(pattern, last_hit, whole);
		}
	}

	// What one pattern is really searched for. A quick search needs a box,
	// so a pattern that has none is searched over the whole frame instead.
	// Only that pattern: widening the whole request would take the boxes of
	// the others away, and which ones lost them would depend on the order
	// they were named in. Ask it before a pass writes down where it really
	// looked, because that is the field it reads.
	const auto scope_wanted = [&](size_t at) {
		return eb::Scope::BOX == result.searched[at].scope
			? scope : eb::Scope::FULL;
	};

	// Hits kept per pattern, so each can be remembered on its own. The
	// answer joins them together. The memo keeps them apart.
	std::vector<std::vector<DetectionHit>> found(images.size());

	// How many hits are still wanted. max_hits counts the whole request, so
	// every hit taken, remembered or freshly found, leaves one less to look
	// for. The search stops as soon as this reaches zero.
	int room {max_hits};

	// What is already known about this frame, pattern by pattern. A pattern
	// that was searched for and missed does not have to be searched again,
	// and one that was found gives its hits towards the count.
	std::vector<bool> known(images.size(), false);
	for (size_t at = 0; at < images.size(); ++at) {
		if (room <= 0) break;
		const eb::Scope asked {scope_wanted(at)};
		for (const Remembered& memo : m_memo) {
			if (!memo.answers(images[at], room, asked)) continue;

			std::vector<DetectionHit> hits = memo.hits;
			if (eb::Scope::BOX == asked && eb::Scope::FULL == memo.scope) {
				// Cut down to the box that was asked about. A hit counts
				// only if the whole pattern fits inside the box, because
				// that is all a search of the box could have found.
				const ImagePattern& pattern = m_library(images[at]);
				const cv::Rect&     box     = result.searched[at].box;
				const cv::Size      size {pattern.width(), pattern.height()};
				hits.erase(
					std::remove_if(
						hits.begin(), hits.end(),
						[&](const DetectionHit& hit) {
							const cv::Rect where {hit.at, size};
							return (box & where) != where;
						}
					),
					hits.end()
				);
				// A whole frame search that stopped at its own limit may
				// have used all its room on hits outside the box. Then it
				// says nothing about the box. It can answer for the box
				// only if enough hits are left, or if the search ran out
				// of candidates instead of room.
				if (hits.size() < static_cast<size_t>(room) && !memo.exhaustive())
					continue;
			}
			rank_hits(hits, room);

			known[at]           = true;
			found[at]           = std::move(hits);
			result.searched[at] = memo.searched;
			room               -= static_cast<int>(found[at].size());
			break;
		}
	}

	// How much room each pattern was really given. Zero means it was never
	// looked at: the count was full before the search reached it.
	std::vector<int> budget(images.size(), 0);
	try {
		// One pattern's share of the work, in whatever area it is looked for.
		const auto look = [&](size_t at, const cv::Rect& area) {
			const size_t pattern {images[at]};
			search_area(captured, area, pattern, budget[at], found[at]);
			if (found[at].empty()) return;

			// Writing down where it was found is what makes the next search
			// quick. A position that did not move is not sent to the cache.
			const cv::Point corner = found[at].front().at;
			if (m_library.set_last_hit(pattern, corner))
				m_cache.store(m_library(pattern), corner);
		};

		// All boxes before any whole frame search: one pattern that moved
		// must not cost the others their quick search.
		for (size_t at = 0; at < images.size(); ++at) {
			if (room <= 0) break;
			if (known[at] || eb::Scope::BOX != result.searched[at].scope)
				continue;
			budget[at] = room;
			look(at, result.searched[at].box);
			room -= static_cast<int>(found[at].size());
		}

		// The whole frame, for every pattern that may be looked for there,
		// until the count is full. A box that filled it on its own spares
		// the others this cost, and they are then reported as never looked
		// at, because that is what they are.
		for (size_t at = 0; at < images.size(); ++at) {
			if (room <= 0) break;
			if (known[at] || eb::Scope::BOX == scope_wanted(at))
				continue;
			// The whole frame holds the box, so this search finds the box
			// hits over again. Give their room back before asking for more,
			// or they would be counted twice and lost from the answer.
			room += static_cast<int>(found[at].size());
			budget[at] = room;
			result.searched[at].scope =
				eb::Scope::BOX == result.searched[at].scope
				? eb::Scope::BOX_THEN_FULL
				: eb::Scope::FULL;
			look(at, whole);
			room -= static_cast<int>(found[at].size());
		}
	}
	catch (const cv::Exception& e) {
		error  = std::string("image matching failed: ") + e.what();
		result = Detection {};
		return false;
	}

	// Each pattern is kept on its own, so a later request finds it here
	// whatever other patterns it is asked with.
	for (size_t at = 0; at < images.size(); ++at) {
		result.hits.insert(
			result.hits.end(),
			found[at].begin(),
			found[at].end()
		);
		if (known[at]) continue;   // came from the memo, already in it
		// A pattern never looked at answers nothing, and must not look as
		// if it did: the plan gave it a box that was never searched.
		if (!budget[at]) {
			result.searched[at] = PatternSearch {images[at]};
			continue;
		}
		// What is kept is the room this one pattern really had, not the
		// count of the whole request. Kept with the area covered, not the
		// name of the request: a box-then-full search that stopped at the
		// box covered the box, and one that went on covered the frame.
		m_memo.push_back(Remembered {
			images[at],
			budget[at],
			eb::Scope::BOX == result.searched[at].scope
				? eb::Scope::BOX
				: eb::Scope::FULL,
			result.searched[at],
			found[at]
		});
	}
	// Nothing was searched this time when no pattern was given any room.
	result.remembered = std::none_of(
		budget.begin(), budget.end(), [](int given) { return given > 0; }
	);
	rank_hits(result.hits, max_hits);
	return true;
}

void ImageDetector::search_area(
	const cv::Mat&             captured,
	const cv::Rect&            area,
	size_t                     image,
	int                        room,
	std::vector<DetectionHit>& hits)
{
	const ImagePattern& pattern = m_library(image);

	// One piece of the frame, converted straight into its own place in
	// m_scene, so a point in m_scene is a point in the frame.
	const auto convert = [this, &captured](const cv::Rect& piece) {
		if (piece.empty()) return;
		cv::cvtColor(captured(piece), m_colour, cv::COLOR_BGRA2BGR);
		cv::Mat into {m_scene(piece)};
		m_colour.convertTo(into, CV_32FC3, COLOUR_SCALE);
	};

	if (m_converted.empty()) {
		convert(area);
		m_converted = area;
	}
	else if ((m_converted & area) != area) {
		// Grow the converted part to hold both rectangles, and convert only
		// the four strips around the old one. Each strip can come out empty,
		// which convert() skips. Together they cover the new pixels exactly,
		// so no pixel is converted twice while the frame lasts.
		const cv::Rect grown {m_converted | area};
		convert({
			grown.x, grown.y,
			grown.width, m_converted.y - grown.y
		});
		convert({
			grown.x, m_converted.br().y,
			grown.width, grown.br().y - m_converted.br().y
		});
		convert({
			grown.x, m_converted.y,
			m_converted.x - grown.x, m_converted.height
		});
		convert({
			m_converted.br().x, m_converted.y,
			grown.br().x - m_converted.br().x, m_converted.height
		});
		m_converted = grown;
	}
	const cv::Mat scene {m_scene(area)};

	cv::matchTemplate(
		scene,
		pattern.image,
		m_match,
		cv::TM_CCOEFF_NORMED,
		pattern.masked() ? pattern.mask : cv::noArray()
	);
	// Normalised correlation divides by the variance under the mask. That
	// variance is zero where both frame and pattern are flat, so those
	// positions come back as NaN or infinity and would win every search.
	cv::patchNaNs(m_match, 0.0f);
	cv::threshold(
		m_match, m_match,
		MAX_VALID_CERTAINTY,
		0.0,
		cv::THRESH_TOZERO_INV
	);

	const int pad_x = pattern.width()  / SUPPRESSION_DIVISOR;
	const int pad_y = pattern.height() / SUPPRESSION_DIVISOR;

	// Both patterns are measured over the same window. It is wide enough to
	// hold the largest lookalike wherever it sits, so a rival one or two
	// pixels off, or a little bigger, still gets its best chance. The
	// candidate is not fixed to the corner the correlation chose, because
	// that corner is its best by another measure.
	int slack = MIN_MARGINE;
	for (const size_t twin : pattern.similar) {
		const ImagePattern& other = m_library(twin);
		slack = std::max(slack, std::abs(other.width()  - pattern.width()));
		slack = std::max(slack, std::abs(other.height() - pattern.height()));
	}

	// The lookalike that took the last candidate is tried first on the next
	// one. On a screen full of one kind of thing it usually takes that one
	// too, and the rest are never measured.
	size_t first_twin = 0;

	hits.clear();
	// Every candidate is blanked below, taken or not, so each pass removes at
	// least one position from a map of finite size. The search ends by itself
	// when nothing is left above the threshold.
	while (static_cast<int>(hits.size()) < room) {
		double certainty {0.0};
		cv::Point at;
		cv::minMaxLoc(m_match, nullptr, &certainty, nullptr, &at);
		if (certainty < pattern.threshold) break;

		// The shape fits. If a pattern it can be confused with matches these
		// pixels better, then it is that pattern and not this one.
		double fit     {0.0};
		bool   claimed {false};
		if (!pattern.similar.empty()) {
			cv::Rect window {
				at.x - slack, at.y - slack,
				pattern.width() + 2 * slack, pattern.height() + 2 * slack
			};
			window &= cv::Rect {0, 0, scene.cols, scene.rows};

			fit = best_fit(scene, window, pattern);
			for (size_t step = 0; step < pattern.similar.size(); ++step) {
				const size_t idx  {(first_twin + step) % pattern.similar.size()},
				             twin {pattern.similar[idx]};
				// Stop at the first one that fits better: measuring the rest
				// cannot change the answer.
				if (best_fit(scene, window, m_library(twin)) < fit) {
					first_twin = idx;
					claimed = true;
					break;
				}
			}
		}
		// matchTemplate positions are top left corners already, so a hit only
		// moves from the match map into frame coordinates.
		if (!claimed)
			hits.push_back(DetectionHit {image, at + area.tl(), certainty, fit});

		// Blank this match either way. A rejected one left in place would be
		// found again on every pass after it.
		const int left   {std::max(0, at.x - pad_x)},
		          top    {std::max(0, at.y - pad_y)},
		          right  {std::min(m_match.cols, at.x + pattern.width()  + pad_x)},
		          bottom {std::min(m_match.rows, at.y + pattern.height() + pad_y)};
		m_match(cv::Rect(left, top, right - left, bottom - top)) = cv::Scalar(0.0);
	}
}

double ImageDetector::best_fit(
	const cv::Mat&      scene,
	const cv::Rect&     window,
	const ImagePattern& pattern)
{
	if (window.width  < pattern.width()
	 || window.height < pattern.height()
	 || pattern.fit_weight <= 0.0)
		return WORST_FIT;

	// TM_SQDIFF here, not the TM_CCOEFF_NORMED the search uses. This step has
	// to see a difference in colour, and CCOEFF removes the mean of each
	// channel before it correlates, which hides exactly that.
	cv::matchTemplate(
		scene(window),
		pattern.image,
		m_fit,
		cv::TM_SQDIFF,
		pattern.masked() ? pattern.mask : cv::noArray()
	);

	double closest {0.0};
	cv::minMaxLoc(m_fit, &closest, nullptr, nullptr, nullptr);

	// Back to the images' own scale, and comparable between patterns of
	// different sizes. Rounding can leave the sum a little below zero.
	return std::sqrt(std::max(0.0, closest) / pattern.fit_weight);
}

void ImageDetector::rank_hits(std::vector<DetectionHit>& hits, int max_hits)
{
	// Best first, from whichever pattern, and never more than were asked
	// for. The count is over the whole request, so hits of several patterns
	// compete for the same places.
	std::stable_sort(
		hits.begin(), hits.end(),
		[](const DetectionHit& one, const DetectionHit& other) {
			return one.certainty > other.certainty;
		}
	);
	if (static_cast<int>(hits.size()) > max_hits)
		hits.resize(static_cast<size_t>(max_hits));
}

cv::Point ImageDetector::search_margines(const ImagePattern& pattern) const
{
	// A fraction given for one axis is measured against that axis. The shared
	// fraction and the default are measured against the longer side. So a
	// wide flat button gets as much room above it as beside it, and that is
	// where a panel with one more line moves it.
	const auto pixels = [this, &pattern](double axis, int size) {
		const double fraction {
			ImagePattern::margine_given(axis)
				? axis
				: ImagePattern::margine_given(pattern.search_margine)
					? pattern.search_margine
					: SEARCH_MARGINE_DEFAULT
		};
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
	const cv::Point margines {search_margines(pattern)};

	// The pattern where it was last seen, widened by the margin and clipped to
	// the frame. Clipping never makes the span smaller than the pattern, which
	// would leave nothing to match.
	const auto quick_span = [](
		int at, int size, int margin, int limit,
		int& start, int& length)
	{
		start   = std::max(0,     at - margin);
		int end = std::min(limit, at + margin + size);
		if (end - start < size) {
			if (end >= limit)
				start = std::max(0, limit - size);
			end = std::min(limit, start + size);
		}
		length = end - start;
	};

	cv::Rect box {frame};
	if (pattern.fixed_x()) quick_span(
		last.x, pattern.width(), margines.x, frame.width,
		box.x, box.width
	);
	if (pattern.fixed_y()) quick_span(
		last.y, pattern.height(), margines.y, frame.height,
		box.y, box.height
	);
	return box;
}
