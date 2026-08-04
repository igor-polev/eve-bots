/*
	EVE bots for Windows.
	Author: Igor Polev.

	Cli implementation.
*/

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "cli.hpp"
#include "png_writer.hpp"
#include "text_util.hpp"

namespace {

// 'dump' waits up to FRAME_WAIT_STEPS * FRAME_WAIT_STEP for a first frame.
constexpr int FRAME_WAIT_STEPS = 40;
constexpr std::chrono::milliseconds FRAME_WAIT_STEP {50};

constexpr const char* HELP_TEXT =
	"Available commands:\n"
	"    help              show this text\n"
	"    find              detect running EVE Online windows\n"
	"    start [n]         start screen capture of window n from the last\n"
	"                      'find' listing; n may be omitted when exactly\n"
	"                      one window was found\n"
	"    stop              stop screen capture\n"
	"    status            show capture state and frame counter\n"
	"    dump [file.png]   write the current frame to a PNG file\n"
	"    images            list the patterns loaded from eve_images.json\n"
	"    detect <image> [n] [quick]\n"
	"                      search the current frame for a pattern, named or\n"
	"                      numbered as 'images' lists it; matches are\n"
	"                      reported by their top left corner. n is how many\n"
	"                      to report at most, 1 by default.\n"
	"                      A pattern with FIXED_DIRECTIONS that has been\n"
	"                      found before is looked for near its last position\n"
	"                      first; 'quick' searches only there and gives up\n"
	"                      instead of scanning the whole frame\n"
	"    click <image> [options]\n"
	"    click <x> <y> [options]\n"
	"                      click the middle of a pattern, or a bare point in\n"
	"                      frame coordinates; see 'click' with no arguments\n"
	"    exit              quit the application\n";

constexpr const char* CLICK_USAGE =
	"Usage: click <image> [options]     the middle of a detected pattern\n"
	"       click <x> <y> [options]     a point, in capture frame pixels\n"
	"Options:\n"
	"    auto        SendInput while the window is active, PostMessage when\n"
	"                it is not - the default\n"
	"    send        always SendInput: system wide, moves the real cursor,\n"
	"                only reaches the foreground window\n"
	"    post        always PostMessage: reaches a background window and\n"
	"                leaves the cursor alone, if the game listens for it\n"
	"    find        search for the pattern when none has been detected yet\n"
	"                (the default); nofind fails instead\n"
	"    refresh     search again even though a position is remembered;\n"
	"                norefresh reuses it (the default)\n"
	"    wait_ms=<n> sleep after the click so the game can react, "
	                 "20 by default\n"
	"'find' and 'refresh' do not apply to clicking a bare point.\n";

std::vector<std::string> tokenize(const std::string& line)
{
	std::string text {line};
	// redirected input often starts with a UTF-8 byte order mark
	if (0 == text.compare(0, 3, "\xEF\xBB\xBF"))
		text.erase(0, 3);

	std::vector<std::string> tokens;
	std::istringstream stream {text};
	std::string token;
	while (stream >> token)
		tokens.push_back(token);
	return tokens;
}

std::string to_lower(std::string text)
{
	std::transform(
		text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(::tolower(c)); }
	);
	return text;
}

// Whole-token integer: "12ab" is not a number, so a mistyped option cannot
// be silently taken for one.
bool parse_int(const std::string& text, int& value)
{
	try {
		size_t used {0};
		const int parsed = std::stoi(text, &used);
		if (used != text.size()) return false;
		value = parsed;
		return true;
	}
	catch (const std::exception&) {
		return false;
	}
}

// Certainties are only meaningful to two or three digits, and formatting
// them here keeps std::cout's flags untouched.
std::string certainty_text(double value)
{
	std::ostringstream text;
	text << std::fixed << std::setprecision(3) << value;
	return text.str();
}

// "a 302x88 box at 3113,300" - where a search actually looked.
std::string box_text(const cv::Rect& box)
{
	std::ostringstream text;
	text << "a " << box.width << "x" << box.height
	     << " box at " << box.x << "," << box.y;
	return text.str();
}

// eve_dump_20260729_143012.png
std::wstring timestamped_name()
{
	auto now = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now()
	);
	std::tm parts {};
	localtime_s(&parts, &now);
	std::wostringstream name;
	name << L"eve_dump_"
	     << std::put_time(&parts, L"%Y%m%d_%H%M%S")
	     << L".png";
	return name.str();
}

} // namespace

int Cli::run()
{
	std::cout << "EVE bots for Windows.\n"
	          << "Settings: " << to_utf8(m_settings.source_path()) << "\n"
	          << "    window class  " << to_utf8(m_settings.eve_window().class_name) << "\n"
	          << "    title prefix  " << to_utf8(m_settings.eve_window().title_prefix) << "\n"
	          << "    capture rate  " << m_settings.capture_frame_rate() << " fps\n"
	          << "    image folder  " << to_utf8(m_settings.image_dir()) << "\n"
	          << "    threshold     " << certainty_text(m_settings.detect_threshold()) << "\n"
	          << "Images: " << to_utf8(m_images.source_path())
	          << " (" << m_images.size()
	          << (1 == m_images.size() ? " pattern)\n" : " patterns)\n");
	if (!ScreenCapture::supported()) {
		std::cout <<
			" [WARNING] Windows Graphics Capture is unavailable on this "
			"system; 'start' will fail.\n";
	}

	std::string error;
	if (!m_detector.start(m_images, m_capture, error)) {
		std::cout << " [WARNING] Image detection is unavailable: "
		          << error << "\n";
	}
	std::cout << "Type 'help' for a list of commands.\n\n";

	std::string line;
	while (true) {
		std::cout << "eve> " << std::flush;
		if (!std::getline(std::cin, line)) {
			// stdin closed (piped input or Ctrl+Z) - quit cleanly
			std::cout << "\n";
			break;
		}
		if (!dispatch(line)) break;
	}
	m_detector.stop();
	m_capture.stop();
	return 0;
}

bool Cli::dispatch(const std::string& line)
{
	const std::vector<std::string> tokens = tokenize(line);
	if (tokens.empty()) return true;

	const std::string command = to_lower(tokens[0]);
	const std::vector<std::string> args {tokens.begin() + 1, tokens.end()};

	if ("exit" == command || "quit" == command)
		return false;
	else if ("help" == command || "?" == command)
		cmd_help();
	else if ("find" == command)
		cmd_find();
	else if ("start" == command)
		cmd_start(args);
	else if ("stop" == command)
		cmd_stop();
	else if ("status" == command)
		cmd_status();
	else if ("dump" == command)
		cmd_dump(args);
	else if ("images" == command)
		cmd_images();
	else if ("detect" == command)
		cmd_detect(args);
	else if ("click" == command)
		cmd_click(args);
	else
		std::cout << "Unknown command: " << tokens[0]
		          << " (type 'help')\n";
	return true;
}

void Cli::cmd_help() const
{
	std::cout << HELP_TEXT;
}

void Cli::print_windows() const
{
	for (size_t i = 0; i < m_windows.size(); ++i) {
		const WindowInfo& info = m_windows[i];
		RECT rect {};
		GetClientRect(info.hwnd, &rect);
		std::cout << "  [" << i << "] "
		          << to_utf8(info.title)
		          << "  (class " << to_utf8(info.class_name)
		          << ", pid "    << info.pid
		          << ", "        << (rect.right - rect.left)
		          << "x"         << (rect.bottom - rect.top)
		          << ")\n";
	}
}

void Cli::cmd_find()
{
	m_windows = find_eve_windows(m_settings.eve_window());
	if (m_windows.empty()) {
		std::cout << "No EVE Online windows found.\n";
		return;
	}
	std::cout << "Found " << m_windows.size()
	          << (1 == m_windows.size() ? " window:\n" : " windows:\n");
	print_windows();
}

void Cli::cmd_start(const std::vector<std::string>& args)
{
	if (m_capture.running()) {
		std::cout << "Capture is already running; use 'stop' first.\n";
		return;
	}
	// a bare 'start' works without a preceding 'find'
	if (m_windows.empty())
		m_windows = find_eve_windows(m_settings.eve_window());

	if (m_windows.empty()) {
		std::cout << "No EVE Online windows found.\n";
		return;
	}

	size_t index {0};
	if (args.empty()) {
		if (m_windows.size() > 1) {
			std::cout << "Several EVE windows are open - "
			             "specify one as 'start <n>':\n";
			print_windows();
			return;
		}
		// exactly one window - no selection needed
	} else {
		try {
			const int parsed = std::stoi(args[0]);
			if (parsed < 0 || static_cast<size_t>(parsed) >= m_windows.size()) {
				std::cout << "No window with index " << args[0]
				          << "; run 'find' to refresh the list.\n";
				return;
			}
			index = static_cast<size_t>(parsed);
		}
		catch (const std::exception&) {
			std::cout << "Not a window number: " << args[0] << "\n";
			return;
		}
	}

	const WindowInfo& target = m_windows[index];
	std::string error;
	if (!m_capture.start(
			target.hwnd, m_settings.capture_frame_rate(), error))
	{
		std::cout << "   [ERROR] Failed to start capture: " << error << "\n";
		return;
	}
	std::cout << "Capture started on [" << index << "] "
	          << to_utf8(target.title)
	          << " at " << m_settings.capture_frame_rate() << " fps\n";
}

void Cli::cmd_stop()
{
	if (!m_capture.running()) {
		std::cout << "Capture is not running.\n";
		return;
	}
	m_capture.stop();
	std::cout << "Capture stopped.\n";
}

void Cli::cmd_status() const
{
	if (!m_capture.running()) {
		std::cout << "Capture: stopped.\n";
		return;
	}
	const Frame frame = m_capture.latest_frame();
	std::cout << "Capture: running at " << m_capture.frame_rate() << " fps, "
	          << m_capture.frame_count() << " frames grabbed ("
	          << m_capture.arrived_count()
	          << " delivered by the window).\n";
	if (frame.empty())
		std::cout << "Last frame: none yet.\n";
	else
		std::cout << "Last frame: "
		          << frame.width << "x" << frame.height << "\n";
}

void Cli::cmd_dump(const std::vector<std::string>& args) const
{
	if (!m_capture.running()) {
		std::cout << "Capture is not running; use 'start' first.\n";
		return;
	}
	// The first frame lands a few milliseconds after 'start', so give the
	// capture thread a moment rather than failing outright.
	Frame frame = m_capture.latest_frame();
	for (int wait = 0; frame.empty() && wait < FRAME_WAIT_STEPS; ++wait) {
		std::this_thread::sleep_for(FRAME_WAIT_STEP);
		frame = m_capture.latest_frame();
	}
	if (frame.empty()) {
		std::cout << "No frame captured yet - is the window minimised?\n";
		return;
	}

	std::wstring path = args.empty()
		? timestamped_name()
		: to_wide(args[0]);
	if (path.size() < 4 ||
		0 != _wcsicmp(path.c_str() + path.size() - 4, L".png"))
	{
		path += L".png";
	}

	std::string error;
	if (!write_png(frame, path, error)) {
		std::cout << "   [ERROR] Failed to write PNG: " << error << "\n";
		return;
	}
	std::cout << "Frame " << frame.width << "x" << frame.height
	          << " written to " << to_utf8(path) << "\n";
}

void Cli::cmd_images() const
{
	if (m_images.empty()) {
		std::cout << "The image library is empty.\n";
		return;
	}
	std::cout << "Image library (" << m_images.size()
	          << (1 == m_images.size() ? " pattern):\n" : " patterns):\n");
	for (size_t image = 0; image < m_images.size(); ++image) {
		const ImagePattern& pattern = m_images(image);
		const cv::Point     last    = m_images.last_hit(image);
		const cv::Point margines = ImageDetector::search_margines(pattern);
		std::cout << "  [" << image << "] " << pattern.name
		          << "  " << pattern.width() << "x" << pattern.height()
		          << (pattern.masked() ? ", alpha mask" : ", opaque")
		          << ", threshold " << certainty_text(pattern.threshold)
		          << ", fixed " << fixed_directions_text(pattern.fixed_directions)
		          << ", margines " << margines.x << "x" << margines.y;
		if (ImageLibrary::seen(last))
			std::cout << ", last seen at " << last.x << "," << last.y;
		std::cout << "\n";
		if (!pattern.comment.empty())
			std::cout << "      " << pattern.comment << "\n";
	}
}

size_t Cli::resolve_image(const std::string& token) const
{
	// 'images' lists patterns with their index, so take either that or the
	// name - the same as 'start <n>' after a 'find'.
	int typed {0};
	if (parse_int(token, typed)) {
		if (typed >= 0 && static_cast<size_t>(typed) < m_images.size())
			return static_cast<size_t>(typed);
		std::cout << "No image with index " << token
		          << "; the library holds " << m_images.size()
		          << (1 == m_images.size() ? " pattern.\n" : " patterns.\n");
		return ImageLibrary::NOT_FOUND;
	}

	const size_t image = m_images.index(token);
	if (ImageLibrary::NOT_FOUND == image) {
		std::cout << "No image called '" << token
		          << "'; the library holds: " << m_images.name_list() << "\n";
	}
	return image;
}

void Cli::cmd_detect(const std::vector<std::string>& args)
{
	if (args.empty()) {
		std::cout << "Usage: detect <image> [count] [quick]\n";
		cmd_images();
		return;
	}

	const size_t image = resolve_image(args[0]);
	if (ImageLibrary::NOT_FOUND == image) return;

	// 'quick' may sit on either side of the count, so pick it out first and
	// read whatever is left as the number of matches.
	int  max_hits {1};
	bool quick {false};
	for (size_t i = 1; i < args.size(); ++i) {
		if ("quick" == to_lower(args[i])) {
			quick = true;
			continue;
		}
		try {
			max_hits = std::stoi(args[i]);
		}
		catch (const std::exception&) {
			std::cout << "Not a number of matches: " << args[i] << "\n";
			return;
		}
		if (max_hits < 1) {
			std::cout << "The number of matches must be 1 or more.\n";
			return;
		}
	}

	const std::string& name = m_images(image).name;
	const auto started = std::chrono::steady_clock::now();

	Detection found;
	std::string error;
	if (!m_detector.detect(image, max_hits, quick, found, error)) {
		std::cout << "   [ERROR] " << error << "\n";
		return;
	}
	const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - started
	);

	std::string where;
	switch (found.scope) {
	case SearchScope::BOX:
		where = ", searched " + box_text(found.box);
		break;
	case SearchScope::BOX_THEN_FULL:
		where = ", " + box_text(found.box) + " missed, searched the whole frame";
		break;
	case SearchScope::FULL:
		where = ", searched the whole frame";
		break;
	}

	if (found.hits.empty()) {
		std::cout << "'" << name << "' not found ("
		          << spent.count() << " ms" << where << ").\n";
		return;
	}
	std::cout << "'" << name << "' found " << found.hits.size()
	          << (1 == found.hits.size() ? " time (" : " times (")
	          << spent.count() << " ms" << where << "):\n";
	for (const DetectionHit& hit : found.hits) {
		std::cout << "  corner " << hit.at.x << "," << hit.at.y
		          << "  certainty " << certainty_text(hit.certainty) << "\n";
	}
}

void Cli::cmd_click(const std::vector<std::string>& args)
{
	if (args.empty()) {
		std::cout << CLICK_USAGE;
		return;
	}

	// Two numbers in a row mean a bare point; anything else names an image.
	// A single number is an image index, so 'click 1' and 'click 1 2' do
	// different things - deliberately, since both readings are useful.
	int    x {0}, y {0};
	bool   point {false};
	size_t first_option {1};
	if (args.size() > 1 && parse_int(args[0], x) && parse_int(args[1], y)) {
		point        = true;
		first_option = 2;
	}

	ClickMethod method  {ClickMethod::AUTO};
	bool        find    {true};
	bool        refresh {false};
	int         wait_ms {UI_WAIT_DEFAULT};
	bool        chosen  {false};   // find or refresh named explicitly

	for (size_t i = first_option; i < args.size(); ++i) {
		const std::string option = to_lower(args[i]);
		if      ("auto"    == option) method  = ClickMethod::AUTO;
		else if ("send"    == option) method  = ClickMethod::SEND;
		else if ("post"    == option) method  = ClickMethod::POST;
		else if ("find"    == option) { find    = true;  chosen = true; }
		else if ("nofind"  == option) { find    = false; chosen = true; }
		else if ("refresh" == option) { refresh = true;  chosen = true; }
		else if ("norefresh" == option) { refresh = false; chosen = true; }
		else if (0 == option.compare(0, 8, "wait_ms=")) {
			if (!parse_int(option.substr(8), wait_ms) || wait_ms < 0) {
				std::cout << "Not a wait in milliseconds: " << args[i] << "\n";
				return;
			}
		} else {
			std::cout << "Unknown click option: " << args[i] << "\n"
			          << CLICK_USAGE;
			return;
		}
	}

	if (point && chosen) {
		std::cout << "'find' and 'refresh' only apply to clicking an image.\n";
		return;
	}
	if (!m_capture.running()) {
		std::cout << "Capture is not running; use 'start' first.\n";
		return;
	}

	cv::Point   target {x, y};
	std::string what;
	if (!point) {
		const size_t image = resolve_image(args[0]);
		if (ImageLibrary::NOT_FOUND == image) return;

		const ImagePattern& pattern = m_images(image);
		cv::Point           corner  = m_images.last_hit(image);

		// 'refresh' asks for a search outright; without it one happens only
		// when there is nothing remembered to click.
		if (refresh || !ImageLibrary::seen(corner)) {
			if (!find && !refresh) {
				std::cout << "'" << pattern.name << "' has not been detected "
				             "yet; run 'detect' first, or drop 'nofind'.\n";
				return;
			}
			Detection found;
			std::string error;
			if (!m_detector.detect(image, 1, false, found, error)) {
				std::cout << "   [ERROR] " << error << "\n";
				return;
			}
			if (found.hits.empty()) {
				std::cout << "'" << pattern.name
				          << "' not found, nothing to click.\n";
				return;
			}
			corner = found.hits.front().at;
		}
		// the corner is where the pattern starts, the middle is what a
		// person would aim at
		target = corner
		       + cv::Point {pattern.width() / 2, pattern.height() / 2};
		what   = "'" + pattern.name + "' at ";
	}

	ClickResult result;
	std::string error;
	if (!click_at(m_capture.target(), target, method, wait_ms, result, error)) {
		std::cout << "   [ERROR] " << error << "\n";
		return;
	}

	const bool posted = ClickMethod::POST == result.method;
	const cv::Point& landed = posted ? result.client : result.screen;
	std::cout << "Clicked " << what
	          << "frame " << result.frame.x << "," << result.frame.y
	          << " -> " << (posted ? "client " : "screen ")
	          << landed.x << "," << landed.y
	          << " (" << click_method_text(result.method) << ")";
	if (wait_ms > 0) std::cout << ", waited " << wait_ms << " ms";
	std::cout << "\n";
}
