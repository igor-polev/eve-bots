/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageLibrary implementation.
*/

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

// Reads a whole file into memory. OpenCV's imread takes a narrow path and
// mangles anything outside the ANSI code page, so decoding from a buffer
// is the only reliable way to open a file under a Unicode path.
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
// is what matchTemplate wants against a float frame.
bool prepare_pattern(const cv::Mat& raw, ImagePattern& pattern, std::string& error)
{
	if (raw.empty()) {
		error = "image holds no pixels";
		return false;
	}
	// 16 bit PNGs are rare but legal, and would come out 256x too bright
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

	if (alpha.empty()) return true;

	double min_alpha {0.0}, max_alpha {0.0};
	cv::minMaxLoc(alpha, &min_alpha, &max_alpha);
	if (max_alpha <= 0.0) {
		error = "image is fully transparent, nothing left to match";
		return false;
	}
	// A channel that is opaque everywhere carries no information; leaving
	// the mask empty then lets matchTemplate take its faster path.
	if (min_alpha >= solid) return true;

	cv::Mat weights;
	alpha.convertTo(weights, CV_32FC1, scale);
	cv::cvtColor(weights, pattern.mask, cv::COLOR_GRAY2BGR);
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
	patterns.reserve(images->size());

	for (size_t i = 0; i < images->size(); ++i) {
		const json& entry = (*images)[i];
		// entries are reported by position as well as by name, because a
		// broken entry may be the one whose name failed to parse
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

	m_patterns  = std::move(patterns);
	m_index     = std::move(index);
	m_directory = dir;
	m_source    = path;
	return true;
}

const ImagePattern* ImageLibrary::find(const std::string& name) const
{
	const auto found = m_index.find(name);
	return m_index.end() == found ? nullptr : &m_patterns[found->second];
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
