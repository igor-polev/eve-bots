/*
	EVE bots for Windows.
	Author: Igor Polev.

	PositionCache implementation.
*/

#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"
#include "position_cache.hpp"
#include "text_util.hpp"

// Keys keep the order they were added in, so the file reads the way it was
// written and not in alphabetical order. The character and the frame size
// stay at the top of each entry, where a reader looks for them.
using json = nlohmann::ordered_json;

namespace {

constexpr const char* KEY_POSITIONS = "POSITIONS";
constexpr const char* KEY_CHARACTER = "CHARACTER";
constexpr const char* KEY_WIDTH     = "WIDTH";
constexpr const char* KEY_HEIGHT    = "HEIGHT";
constexpr const char* KEY_IMAGES    = "IMAGES";
constexpr const char* KEY_X         = "X";
constexpr const char* KEY_Y         = "Y";

// Indentation of the written file. People are meant to read it and, when
// something looks wrong, edit it by hand.
constexpr int INDENT = 4;

// One coordinate of a cached corner. A missing key means the pattern does
// not stay in place along that axis, which is not an error.
bool read_axis(const json& position, const char* key, int& value)
{
	const auto found = position.find(key);
	if (position.end() == found) return true;
	if (!found->is_number_integer()) return false;

	const long long read = found->get<long long>();
	if (read < 0) return false;
	value = static_cast<int>(read);
	return true;
}

} // namespace

bool PositionCache::load(const std::wstring& path, std::string& error)
{
	std::lock_guard<std::mutex> lock {m_mutex};
	m_path = path;
	m_entries.clear();
	m_current = NONE;

	// On a first run there is nothing to read and nothing to complain about.
	if (!file_exists(path)) return true;

	std::ifstream file {path};
	if (!file.is_open()) {
		error = "cannot open " + to_utf8(path);
		return false;
	}

	json cached;
	try {
		cached = json::parse(file);
	}
	catch (const json::parse_error& e) {
		error = "failed to parse " + to_utf8(path) + ":\n    " + e.what();
		return false;
	}

	const auto entries = cached.find(KEY_POSITIONS);
	if (cached.end() == entries || !entries->is_array()) {
		error = to_utf8(path) + " must hold an array called " + KEY_POSITIONS;
		return false;
	}

	std::vector<Entry> parsed;
	for (size_t i = 0; i < entries->size(); ++i) {
		const json& source = (*entries)[i];
		const std::string where =
			std::string(KEY_POSITIONS) + "[" + std::to_string(i) + "] in "
			+ to_utf8(path);

		if (!source.is_object()) {
			error = where + " is not an object";
			return false;
		}

		Entry entry;
		const auto character = source.find(KEY_CHARACTER);
		const auto width     = source.find(KEY_WIDTH);
		const auto height    = source.find(KEY_HEIGHT);
		if (source.end() == character || !character->is_string() ||
			source.end() == width     || !width->is_number_integer() ||
			source.end() == height    || !height->is_number_integer())
		{
			error = where + " needs " + KEY_CHARACTER + ", " + KEY_WIDTH
			      + " and " + KEY_HEIGHT;
			return false;
		}
		entry.character = character->get<std::string>();
		entry.width     = width->get<int>();
		entry.height    = height->get<int>();

		const auto images = source.find(KEY_IMAGES);
		if (source.end() != images) {
			if (!images->is_object()) {
				error = where + ": " + KEY_IMAGES
				      + " must hold one position per image name";
				return false;
			}
			for (const auto& [name, position] : images->items()) {
				Position corner;
				if (!position.is_object() ||
					!read_axis(position, KEY_X, corner.x) ||
					!read_axis(position, KEY_Y, corner.y))
				{
					error = where + ": position of '" + name + "' must hold "
					      + KEY_X + " and/or " + KEY_Y
					      + " as whole numbers, none of them negative";
					return false;
				}
				entry.images[name] = corner;
			}
		}
		parsed.push_back(std::move(entry));
	}

	m_entries = std::move(parsed);
	return true;
}

PositionCache::Follow PositionCache::follow(
	ImageLibrary&      images,
	const std::string& character,
	int                width,
	int                height)
{
	std::lock_guard<std::mutex> lock {m_mutex};

	// Already on this client. The library holds everything the file holds
	// and more, because the free axes live only in memory.
	if (NONE != m_current) {
		const Entry& followed = m_entries[m_current];
		if (followed.character == character &&
			followed.width == width && followed.height == height)
		{
			return Follow {};
		}
	}

	// A different client, so what is remembered belongs somewhere else.
	images.forget_hits();

	m_current = NONE;
	for (size_t at = 0; at < m_entries.size(); ++at) {
		if (m_entries[at].character == character &&
			m_entries[at].width == width && m_entries[at].height == height)
		{
			m_current = at;
			break;
		}
	}
	if (NONE == m_current) {
		// Nothing filed for this client yet. The entry starts empty and
		// fills up as patterns are found.
		m_entries.push_back(Entry {character, width, height, {}});
		m_current = m_entries.size() - 1;
		return Follow {true, 0};
	}

	Follow result {true, 0};
	for (const auto& [name, corner] : m_entries[m_current].images) {
		// A name the library no longer knows is left in the file, not
		// dropped. It costs one line, and a pattern removed from
		// eve_images.json for an afternoon comes back to its old position.
		const size_t image = images.index(name);
		if (ImageLibrary::NOT_FOUND == image) continue;

		// Only the axes the pattern stays in place along today. The file may
		// have been written when FIXED_DIRECTIONS said something else, and a
		// coordinate that is free now is worse than no coordinate.
		const ImagePattern& pattern = images(image);
		const cv::Point restored {
			pattern.fixed_x() ? corner.x : ImageLibrary::UNKNOWN,
			pattern.fixed_y() ? corner.y : ImageLibrary::UNKNOWN
		};
		if (!ImageLibrary::seen(restored)) continue;

		images.set_last_hit(image, restored);
		++result.restored;
	}
	return result;
}

bool PositionCache::store(const ImagePattern& pattern, const cv::Point& corner)
{
	// A pattern that can appear anywhere is never searched for in a box, so
	// where it was last time is of no use.
	if (FIXED_NONE == pattern.fixed_directions) return false;

	std::lock_guard<std::mutex> lock {m_mutex};
	if (NONE == m_current) return false;   // no client to file it under

	const Position wanted {
		pattern.fixed_x() ? corner.x : ImageLibrary::UNKNOWN,
		pattern.fixed_y() ? corner.y : ImageLibrary::UNKNOWN
	};

	std::map<std::string, Position>& kept = m_entries[m_current].images;
	const auto found = kept.find(pattern.name);
	const bool known = kept.end() != found;
	if (known && found->second == wanted)
		return false;                      // moved, but not where it counts

	const Position previous = known ? found->second : Position {};
	kept[pattern.name] = wanted;

	std::string trouble;
	if (save(trouble)) {
		m_error.clear();
		return true;
	}
	// Put the data in memory back to what the file really holds, so the next
	// detection writes again instead of believing this write worked.
	if (known) kept[pattern.name] = previous;
	else       kept.erase(pattern.name);
	m_error = trouble;
	return false;
}

bool PositionCache::save(std::string& error) const
{
	if (m_path.empty()) {
		error = "no file to write positions to";
		return false;
	}

	json cached;
	json& entries = cached[KEY_POSITIONS] = json::array();
	for (const Entry& entry : m_entries) {
		json written;
		written[KEY_CHARACTER] = entry.character;
		written[KEY_WIDTH]     = entry.width;
		written[KEY_HEIGHT]    = entry.height;

		json& images = written[KEY_IMAGES] = json::object();
		for (const auto& [name, corner] : entry.images) {
			json position = json::object();
			if (corner.x > ImageLibrary::UNKNOWN) position[KEY_X] = corner.x;
			if (corner.y > ImageLibrary::UNKNOWN) position[KEY_Y] = corner.y;
			// An entry with neither axis says nothing. Leaving it out keeps
			// lines that mean "nothing known" out of the file.
			if (!position.empty()) images[name] = std::move(position);
		}
		entries.push_back(std::move(written));
	}

	std::ofstream file {m_path, std::ios::trunc};
	if (!file.is_open()) {
		error = "cannot write " + to_utf8(m_path);
		return false;
	}
	file << cached.dump(INDENT) << "\n";
	if (!file) {
		error = "failed to write " + to_utf8(m_path);
		return false;
	}
	return true;
}

std::string PositionCache::last_error() const
{
	std::lock_guard<std::mutex> lock {m_mutex};
	return m_error;
}

std::string PositionCache::state_text() const
{
	std::lock_guard<std::mutex> lock {m_mutex};
	if (NONE == m_current)
		return "no client followed yet - start capture to remember positions";

	const Entry& entry = m_entries[m_current];
	std::string text =
		"'" + entry.character + "' at " + std::to_string(entry.width) + "x"
		+ std::to_string(entry.height) + ", "
		+ std::to_string(entry.images.size())
		+ (1 == entry.images.size() ? " position" : " positions")
		+ " in " + to_utf8(m_path);
	if (!m_error.empty()) text += "\n   [ERROR] " + m_error;
	return text;
}
