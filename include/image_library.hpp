/*
	EVE bots for Windows.
	Author: Igor Polev.

	ImageLibrary - the set of patterns the detector can look for,
	described by eve_images.json and stored as PNG files.

	Patterns are used at their stored size: the capture resolution is
	whatever the EVE window is, so reference images must be cut from a
	screenshot taken at the resolution the bot will run at.

	A pattern PNG may carry an alpha channel. Transparent pixels are turned
	into a mask, so only the opaque part of the image has to match - that is
	how a button can be recognised without its changing background.
*/

#pragma once
#include <map>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

struct ImagePattern {
	std::string  name;     // key the 'detect' command uses
	std::wstring file;     // path as written in eve_images.json
	std::wstring path;     // where the file was actually read from
	std::string  comment;  // optional, for humans only
	double       threshold {0.0}; // certainty a match must reach to count

	cv::Mat image;  // CV_32FC3, BGR, values 0..1
	cv::Mat mask;   // CV_32FC3 weights; empty when the PNG is fully opaque

	int  width()  const noexcept { return image.cols; }
	int  height() const noexcept { return image.rows; }
	bool masked() const noexcept { return !mask.empty(); }
};

class ImageLibrary {
public:
	static constexpr const wchar_t* FILE_NAME = L"eve_images.json";

	// Looks for FILE_NAME in the working directory, then next to the
	// executable, and reads every PNG it names from dir.
	// default_threshold applies to patterns that name none of their own.
	// On failure returns false and fills error.
	bool load(
		const std::wstring& dir, double default_threshold, std::string& error
	);

	// Loads a specific description file.
	bool load_file(
		const std::wstring& path,
		const std::wstring& dir,
		double              default_threshold,
		std::string&        error
	);

	// Path the description was read from; empty until loaded.
	const std::wstring& source_path() const noexcept { return m_source; }
	const std::wstring& directory()   const noexcept { return m_directory; }

	bool   empty() const noexcept { return m_patterns.empty(); }
	size_t size()  const noexcept { return m_patterns.size(); }
	const std::vector<ImagePattern>& patterns() const noexcept
		{ return m_patterns; }

	// Pattern with this name, or nullptr when there is none.
	const ImagePattern* find(const std::string& name) const;

	// Comma separated list of the names, for error messages.
	std::string name_list() const;

private:
	std::wstring m_source;
	std::wstring m_directory;
	std::vector<ImagePattern> m_patterns;
	std::map<std::string, size_t> m_index;
};
