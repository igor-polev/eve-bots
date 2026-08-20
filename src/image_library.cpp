/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageLibrary implementation.
*/

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "image_library.hpp"
#include "paths.hpp"
#include "text_util.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* KEY_IMAGES    = "IMAGES";
constexpr const char* KEY_NAME      = "NAME";
constexpr const char* KEY_FILE      = "FILE";
constexpr const char* KEY_COMMENT   = "COMMENT";
constexpr const char* KEY_THRESHOLD = "THRESHOLD";
constexpr const char* KEY_FIXED     = "FIXED_DIRECTIONS";
constexpr const char* KEY_MARGINE   = "SEARCH_MARGINE";
constexpr const char* KEY_MARGINE_X = "SEARCH_MARGINE_X";
constexpr const char* KEY_MARGINE_Y = "SEARCH_MARGINE_Y";
constexpr const char* KEY_SIMILAR   = "SIMILAR";

// Reads one optional search margin. A missing key leaves the fraction as it
// was, and that is how ImageDetector later finds the ones it must fill in.
bool read_margine(
	const json&        entry,
	const char*        key,
	const std::string& where,
	double&            fraction,
	std::string&       error)
{
	const auto found = entry.find(key);
	if (entry.end() == found) return true;

	if (!found->is_number()) {
		error = where + ": " + key + " must be a number";
		return false;
	}
	fraction = found->get<double>();
	if (fraction < 0.0) {
		error = where + ": " + key + " must not be negative (got "
		      + std::to_string(fraction) + ")";
		return false;
	}
	return true;
}

// Turns the FIXED_DIRECTIONS text into a bit set. Order and case do not
// matter. Anything else is refused by the caller.
bool parse_fixed_directions(const std::string& text, unsigned& directions)
{
	directions = FIXED_NONE;
	for (const char c : text) {
		switch (::toupper(static_cast<unsigned char>(c))) {
		case 'X': directions |= FIXED_X; break;
		case 'Y': directions |= FIXED_Y; break;
		default:  return false;
		}
	}
	return FIXED_NONE != directions;
}

// Reads a whole file into memory. OpenCV's imread takes a narrow path and
// breaks anything outside the ANSI code page, so decoding from a buffer is
// the only safe way to open a file under a Unicode path.
bool read_file_bytes(
	const std::wstring& path, std::vector<uchar>& bytes, std::string& error)
{
	std::ifstream file {path, std::ios::binary | std::ios::ate};
	if (!file.is_open()) {
		error = "cannot open " + to_utf8(path);
		return false;
	}
	const std::streamoff size = file.tellg();
	if (size <= 0) {
		error = to_utf8(path) + " is empty";
		return false;
	}
	bytes.resize(static_cast<size_t>(size));
	file.seekg(0, std::ios::beg);
	if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
		error = "failed to read " + to_utf8(path);
		return false;
	}
	return true;
}

// Splits a decoded PNG into a BGR template and, when the image is not fully
// opaque, an alpha mask. Both come out as CV_32FC3 with values 0..1, which
// is what matchTemplate needs against a float frame.
bool prepare_pattern(const cv::Mat& raw, ImagePattern& pattern, std::string& error)
{
	if (raw.empty()) {
		error = "image holds no pixels";
		return false;
	}
	// 16 bit PNGs are rare but allowed, and would come out 256 times too bright
	const bool   deep  = CV_16U == raw.depth();
	const double scale = deep ? 1.0 / 65535.0 : 1.0 / 255.0;
	const double solid = deep ? 65535.0 : 255.0;

	cv::Mat colour, alpha;
	std::vector<cv::Mat> channels;
	switch (raw.channels()) {
	case 1:                                    // grey, no transparency
		cv::cvtColor(raw, colour, cv::COLOR_GRAY2BGR);
		break;
	case 2:                                    // grey + alpha
		cv::split(raw, channels);
		cv::cvtColor(channels[0], colour, cv::COLOR_GRAY2BGR);
		alpha = channels[1];
		break;
	case 3:                                    // BGR, no transparency
		colour = raw;
		break;
	case 4:                                    // BGR + alpha
		cv::split(raw, channels);
		cv::merge(std::vector<cv::Mat> {channels[0], channels[1], channels[2]},
		          colour);
		alpha = channels[3];
		break;
	default:
		error = "unsupported channel count "
		      + std::to_string(raw.channels());
		return false;
	}

	colour.convertTo(pattern.image, CV_32FC3, scale);
	pattern.fit_weight = static_cast<double>(pattern.width())
	                   * pattern.height() * pattern.image.channels();

	if (alpha.empty()) return true;

	double min_alpha {0.0}, max_alpha {0.0};
	cv::minMaxLoc(alpha, &min_alpha, &max_alpha);
	if (max_alpha <= 0.0) {
		error = "image is fully transparent, nothing left to match";
		return false;
	}
	// A channel that is opaque everywhere says nothing. Leaving the mask
	// empty then lets matchTemplate use its faster path.
	if (min_alpha >= solid) return true;

	cv::Mat weights;
	alpha.convertTo(weights, CV_32FC1, scale);
	cv::cvtColor(weights, pattern.mask, cv::COLOR_GRAY2BGR);

	// Masked TM_SQDIFF multiplies the difference by the mask before squaring,
	// so the weight is squared in the sum as well. The L2 norm over all
	// channels is the square root of exactly that sum.
	const double norm {cv::norm(pattern.mask, cv::NORM_L2)};
	pattern.fit_weight = norm * norm;
	return true;
}

} // namespace

bool ImageLibrary::load(
	const std::wstring& dir, double default_threshold, std::string& error)
{
	std::vector<std::wstring> tried;
	const std::wstring path = find_config_file(FILE_NAME, &tried);
	if (path.empty()) {
		error = to_utf8(FILE_NAME) + std::string(" not found; looked in:");
		for (const std::wstring& candidate : tried)
			error += "\n    " + to_utf8(candidate);
		return false;
	}
	return load_file(path, dir, default_threshold, error);
}

bool ImageLibrary::load_file(
	const std::wstring& path,
	const std::wstring& dir,
	double              default_threshold,
	std::string&        error)
{
	std::ifstream file {path};
	if (!file.is_open()) {
		error = "cannot open " + to_utf8(path);
		return false;
	}

	json description;
	try {
		description = json::parse(file);
	}
	catch (const json::parse_error& e) {
		error = "failed to parse " + to_utf8(path) + ":\n    " + e.what();
		return false;
	}

	const auto images = description.find(KEY_IMAGES);
	if (images == description.end() || !images->is_array()) {
		error = to_utf8(path) + " must hold an array called " + KEY_IMAGES;
		return false;
	}
	if (images->empty()) {
		error = std::string(KEY_IMAGES) + " in " + to_utf8(path) + " is empty";
		return false;
	}

	std::vector<ImagePattern>     patterns;
	std::map<std::string, size_t> index;
	// SIMILAR entries as written, one list per pattern, in the same order
	// as patterns. They become indexes once every name is known.
	std::vector<std::vector<std::string>> named;
	m_warnings.clear();
	patterns.reserve(images->size());

	for (size_t i = 0; i < images->size(); ++i) {
		const json& entry = (*images)[i];
		// entries are reported by number as well as by name, because a
		// broken entry may be the one whose name could not be read
		const std::string where =
			std::string(KEY_IMAGES) + "[" + std::to_string(i) + "] in "
			+ to_utf8(path);

		if (!entry.is_object()) {
			error = where + " is not an object";
			return false;
		}

		ImagePattern pattern;
		try {
			const json& name = entry.at(KEY_NAME);
			const json& file_name = entry.at(KEY_FILE);
			if (!name.is_string() || !file_name.is_string()) {
				error = where + ": " + KEY_NAME + " and " + KEY_FILE
				      + " must be strings";
				return false;
			}
			pattern.name = name.get<std::string>();
			pattern.file = to_wide(file_name.get<std::string>());
		}
		catch (const json::out_of_range&) {
			error = where + " needs both " + KEY_NAME + " and " + KEY_FILE;
			return false;
		}

		if (pattern.name.empty() || pattern.file.empty()) {
			error = where + ": " + KEY_NAME + " and " + KEY_FILE
			      + " must not be empty";
			return false;
		}
		if (index.count(pattern.name)) {
			error = where + ": duplicate name '" + pattern.name + "'";
			return false;
		}

		const auto comment = entry.find(KEY_COMMENT);
		if (comment != entry.end()) {
			if (!comment->is_string()) {
				error = where + ": " + KEY_COMMENT + " must be a string";
				return false;
			}
			pattern.comment = comment->get<std::string>();
		}

		pattern.threshold = default_threshold;
		const auto threshold = entry.find(KEY_THRESHOLD);
		if (threshold != entry.end()) {
			if (!threshold->is_number()) {
				error = where + ": " + KEY_THRESHOLD + " must be a number";
				return false;
			}
			pattern.threshold = threshold->get<double>();
			if (pattern.threshold <= 0.0 || pattern.threshold > 1.0) {
				error = where + ": " + KEY_THRESHOLD
				      + " must be greater than 0 and at most 1 (got "
				      + std::to_string(pattern.threshold) + ")";
				return false;
			}
		}

		const auto fixed = entry.find(KEY_FIXED);
		if (fixed != entry.end()) {
			if (!fixed->is_string() ||
				!parse_fixed_directions(
					fixed->get<std::string>(), pattern.fixed_directions))
			{
				error = where + ": " + KEY_FIXED
				      + " must be \"X\", \"Y\" or \"XY\"; leave it out when "
				        "the image can turn up anywhere";
				return false;
			}
		}

		if (!read_margine(
				entry, KEY_MARGINE, where, pattern.search_margine, error) ||
			!read_margine(
				entry, KEY_MARGINE_X, where, pattern.search_margine_x, error) ||
			!read_margine(
				entry, KEY_MARGINE_Y, where, pattern.search_margine_y, error))
		{
			return false;
		}

		// Kept as names for now. A pattern may name another one that has
		// not been read yet, so names become indexes only when all are in.
		std::vector<std::string> similar;
		const auto listed = entry.find(KEY_SIMILAR);
		if (listed != entry.end()) {
			if (!listed->is_array()) {
				error = where + ": " + KEY_SIMILAR
				      + " must be a list of image names";
				return false;
			}
			for (const json& other : *listed) {
				if (!other.is_string()) {
					error = where + ": every name in " + KEY_SIMILAR
					      + " must be a string";
					return false;
				}
				similar.push_back(other.get<std::string>());
			}
		}
		named.push_back(std::move(similar));

		pattern.path = join_path(dir, pattern.file);
		std::vector<uchar> bytes;
		if (!read_file_bytes(pattern.path, bytes, error)) {
			error = "image '" + pattern.name + "': " + error;
			return false;
		}

		cv::Mat raw;
		try {
			raw = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
		}
		catch (const cv::Exception& e) {
			error = "image '" + pattern.name + "' (" + to_utf8(pattern.path)
			      + "): " + e.what();
			return false;
		}
		if (raw.empty()) {
			error = "image '" + pattern.name + "': "
			      + to_utf8(pattern.path) + " is not a readable image";
			return false;
		}
		if (!prepare_pattern(raw, pattern, error)) {
			error = "image '" + pattern.name + "' ("
			      + to_utf8(pattern.path) + "): " + error;
			return false;
		}

		index[pattern.name] = patterns.size();
		patterns.push_back(std::move(pattern));
	}

	if (!link_similar(patterns, index, named, path, error)) return false;

	{
		std::lock_guard<std::mutex> lock {m_hits_mutex};
		m_last_hits.assign(patterns.size(), NEVER_SEEN);
	}
	m_patterns  = std::move(patterns);
	m_index     = std::move(index);
	m_directory = dir;
	m_source    = path;
	return true;
}

bool ImageLibrary::link_similar(
	std::vector<ImagePattern>&                  patterns,
	const std::map<std::string, size_t>&        index,
	const std::vector<std::vector<std::string>>& named,
	const std::wstring&                         path,
	std::string&                                error)
{
	for (size_t at = 0; at < patterns.size(); ++at) {
		ImagePattern& pattern = patterns[at];

		for (const std::string& other : named[at]) {
			// A name that is not in the library can only be a typo. Letting
			// it through would quietly drop the very protection it asks
			// for.
			const auto found = index.find(other);
			if (index.end() == found) {
				error = "image '" + pattern.name + "' in " + to_utf8(path)
				      + ": " + KEY_SIMILAR + " names '" + other
				      + "', which is not in the library";
				return false;
			}
			const size_t twin = found->second;

			if (twin == at) {
				m_warnings.push_back(
					"image '" + pattern.name + "': " + KEY_SIMILAR
					+ " lists itself, ignored"
				);
				continue;
			}
			if (pattern.similar.end() != std::find(
					pattern.similar.begin(), pattern.similar.end(), twin))
			{
				m_warnings.push_back(
					"image '" + pattern.name + "': " + KEY_SIMILAR
					+ " lists '" + other + "' more than once"
				);
				continue;
			}
			pattern.similar.push_back(twin);
		}
	}

	// Similarity works both ways, whether or not the file says so twice, so
	// naming it on one of the two patterns is enough.
	for (size_t at = 0; at < patterns.size(); ++at) {
		for (const size_t twin : patterns[at].similar) {
			eb::Images& back = patterns[twin].similar;
			if (back.end() == std::find(back.begin(), back.end(), at))
				back.push_back(at);
		}
	}

	// A pattern shares a search window only with something near its own size,
	// so a big difference is more likely a mistake than a lookalike.
	for (const ImagePattern& pattern : patterns) {
		for (const size_t twin : pattern.similar) {
			const ImagePattern& other = patterns[twin];
			if (other.name < pattern.name) continue;   // report each pair once

			const double spread = std::max(
				std::abs(pattern.width()  - other.width())
					/ static_cast<double>(std::max(pattern.width(),  other.width())),
				std::abs(pattern.height() - other.height())
					/ static_cast<double>(std::max(pattern.height(), other.height()))
			);
			if (spread > ImagePattern::SIMILAR_SIZE_SPREAD) {
				m_warnings.push_back(
					"images '" + pattern.name + "' and '" + other.name
					+ "' are declared similar but differ in size by "
					+ std::to_string(static_cast<int>(spread * 100.0)) + "%"
				);
			}
		}
	}
	return true;
}

size_t ImageLibrary::index(const std::string& name) const
{
	const auto found = m_index.find(name);
	return m_index.end() == found ? NOT_FOUND : found->second;
}

std::string ImageLibrary::name_list() const
{
	std::string list;
	for (const ImagePattern& pattern : m_patterns) {
		if (!list.empty()) list += ", ";
		list += pattern.name;
	}
	return list;
}

std::string ImageLibrary::names_text(const eb::Images& images) const
{
	std::string text;
	for (size_t at = 0; at < images.size(); ++at) {
		if (at > 0) text += (at + 1 == images.size()) ? " or " : ", ";
		text += "'" + m_patterns[images[at]].name + "'";
	}
	return text;
}

cv::Point ImageLibrary::last_hit(size_t image) const
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	return m_last_hits[image];
}

bool ImageLibrary::set_last_hit(size_t image, const cv::Point& corner)
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	cv::Point& hit = m_last_hits[image];
	if (hit == corner) return false;
	hit = corner;
	return true;
}

void ImageLibrary::forget_hits()
{
	std::lock_guard<std::mutex> lock {m_hits_mutex};
	m_last_hits.assign(m_patterns.size(), NEVER_SEEN);
}

std::string fixed_directions_text(unsigned directions)
{
	std::string text;
	if (0 != (directions & FIXED_X)) text += 'X';
	if (0 != (directions & FIXED_Y)) text += 'Y';
	return text.empty() ? "-" : text;
}
