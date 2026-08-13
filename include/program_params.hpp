/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramParams - what prog_params.json says about each program.

	Programs carry their own defaults, so this file only ever overrides
	them. That is why a missing file is not fatal the way missing settings
	or a missing image library are: without it every program still knows
	what to do. A file that is present but malformed is fatal, because a
	parameter that silently fails to apply is worse than a refusal to run.
*/

#pragma once

#include <map>
#include <string>
#include <vector>

class ProgramParams {
public:
	static constexpr const wchar_t* FILE_NAME = L"prog_params.json";

	// Looks for FILE_NAME in the working directory, then next to the
	// executable. Returns false only when a file was found and could not
	// be understood; a file that is simply absent leaves loaded() false.
	bool load(std::string& error);
	bool load_file(const std::wstring& path, std::string& error);

	const std::wstring& source_path() const noexcept { return m_source; }
	bool loaded() const noexcept { return !m_source.empty(); }

	bool has(const std::string& program) const;
	// Whether the file says anything about one parameter, which is not the
	// same as what its value came out as: a program's built in default and
	// a file that repeats it are indistinguishable by value alone.
	bool has(const std::string& program, const std::string& key) const;

	// One parameter of one program, or fallback when either is missing.
	double number(
		const std::string& program, const std::string& key, double fallback
	) const;
	std::string text(
		const std::string& program,
		const std::string& key,
		const std::string& fallback
	) const;

	// Programs named in the file, in the order they appear.
	const std::vector<std::string>& names() const noexcept { return m_names; }

	// Every parameter the file names for one program, so that a key no
	// program knows can be reported rather than quietly ignored.
	std::vector<std::string> keys(const std::string& program) const;

private:
	// Parameters are kept by type rather than as raw JSON so that nothing
	// outside this class has to know the file format.
	struct Values {
		std::map<std::string, double>      numbers;
		std::map<std::string, std::string> texts;
	};

	std::wstring                  m_source;
	std::vector<std::string>      m_names;
	std::map<std::string, Values> m_programs;
};
