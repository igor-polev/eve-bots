/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramParams implementation.
*/

#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"
#include "program_params.hpp"
#include "text_util.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* KEY_PROGRAMS = "PROGRAMS";
constexpr const char* KEY_NAME     = "NAME";
constexpr const char* KEY_COMMENT  = "COMMENT";

} // namespace

bool ProgramParams::load(std::string& error)
{
	const std::wstring path = find_config_file(FILE_NAME);
	if (path.empty()) return true;   // absent is allowed, see the header
	return load_file(path, error);
}

bool ProgramParams::load_file(const std::wstring& path, std::string& error)
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

	const auto programs = description.find(KEY_PROGRAMS);
	if (programs == description.end() || !programs->is_array()) {
		error = to_utf8(path) + " must hold an array called " + KEY_PROGRAMS;
		return false;
	}

	std::vector<std::string>      names;
	std::map<std::string, Values> parsed;

	for (size_t i = 0; i < programs->size(); ++i) {
		const json& entry = (*programs)[i];
		const std::string where =
			std::string(KEY_PROGRAMS) + "[" + std::to_string(i) + "] in "
			+ to_utf8(path);

		if (!entry.is_object()) {
			error = where + " is not an object";
			return false;
		}
		const auto name = entry.find(KEY_NAME);
		if (entry.end() == name || !name->is_string()) {
			error = where + " needs a " + KEY_NAME + " string";
			return false;
		}
		const std::string program = name->get<std::string>();
		if (program.empty()) {
			error = where + ": " + KEY_NAME + " must not be empty";
			return false;
		}
		if (parsed.count(program)) {
			error = where + ": duplicate program '" + program + "'";
			return false;
		}

		Values values;
		for (auto item = entry.begin(); item != entry.end(); ++item) {
			// NAME identifies the entry and COMMENT is for humans; neither
			// is a parameter any program should see.
			if (KEY_NAME == item.key() || KEY_COMMENT == item.key())
				continue;

			if (item->is_number())
				values.numbers[item.key()] = item->get<double>();
			else if (item->is_boolean())
				values.numbers[item.key()] = item->get<bool>() ? 1.0 : 0.0;
			else if (item->is_string())
				values.texts[item.key()] = item->get<std::string>();
			else {
				error = where + ": " + item.key()
				      + " must be a number, a boolean or a string";
				return false;
			}
		}

		names.push_back(program);
		parsed[program] = std::move(values);
	}

	m_names    = std::move(names);
	m_programs = std::move(parsed);
	m_source   = path;
	return true;
}

bool ProgramParams::has(const std::string& program) const
{
	return 0 != m_programs.count(program);
}

double ProgramParams::number(
	const std::string& program, const std::string& key, double fallback) const
{
	const auto found = m_programs.find(program);
	if (m_programs.end() == found) return fallback;

	const auto value = found->second.numbers.find(key);
	return found->second.numbers.end() == value ? fallback : value->second;
}

std::string ProgramParams::text(
	const std::string& program,
	const std::string& key,
	const std::string& fallback) const
{
	const auto found = m_programs.find(program);
	if (m_programs.end() == found) return fallback;

	const auto value = found->second.texts.find(key);
	return found->second.texts.end() == value ? fallback : value->second;
}
