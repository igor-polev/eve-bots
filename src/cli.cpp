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
#include "prg/prg_registry.hpp"
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
	"    status            show capture state, frame counter and which\n"
	"                      client's remembered positions are in use\n"
	"    dump [file.png]   write the current frame to a PNG file\n"
	"    images            list the patterns loaded from eve_images.json\n"
	"    detect <image>[,<image>...] [n] [quick]\n"
	"                      search the current frame for a pattern, named or\n"
	"                      numbered as 'images' lists it; matches are\n"
	"                      reported by their top left corner. n is how many\n"
	"                      to report at most, 1 by default.\n"
	"                      Several patterns separated by commas mean any of\n"
	"                      them will do: one search, and each of the n\n"
	"                      matches may come from any pattern in the list.\n"
	"                      A pattern with FIXED_DIRECTIONS that has been\n"
	"                      found before is looked for near its last position\n"
	"                      first; 'quick' searches only there and gives up\n"
	"                      instead of scanning the whole frame\n"
	"    click <image> [options]\n"
	"    click <x> <y> [options]\n"
	"                      click the middle of a pattern, or a bare point in\n"
	"                      frame coordinates. 'confirm=<image>' makes the\n"
	"                      click wait for proof that the game took it and\n"
	"                      click again if it did not; see 'click' with no\n"
	"                      arguments for that and the rest of the options\n"
	"    programs          list the programs and the parameters they will use\n"
	"    run <program>     start a program on its own thread; the console\n"
	"                      stays usable and the outcome is printed when it\n"
	"                      finishes\n"
	"    abort             ask the running program to stop early\n"
	"    exit              quit the application\n"
	"The hotkey named by MENU_HOTKEY in the settings brings the same list\n"
	"of programs up over the captured window: click one to start it, or\n"
	"Abort to stop whatever is running. Press it again, press Escape or\n"
	"click elsewhere to put it away.\n";

constexpr const char* CLICK_USAGE =
	"Usage: click <image> [options]     the middle of a detected pattern\n"
	"       click <x> <y> [options]     a point, in capture frame pixels\n"
	"The click is made with SendInput, which means the game is brought to\n"
	"the front first and the real cursor is moved: a click on an inactive\n"
	"window is swallowed to activate it instead of acting.\n"
	"Options:\n"
	"    find        search for the pattern when none has been detected yet\n"
	"                (the default); nofind fails instead\n"
	"    refresh     search again even though a position is remembered;\n"
	"                norefresh reuses it (the default)\n"
	"    wait_ms=<n> sleep after the click so the game can react, "
	                 "20 by default\n"
	"    confirm=<image>[,<image>...]\n"
	"                what the click is supposed to bring about. After the\n"
	"                click these are searched for until one turns up; any\n"
	"                of them will do. Without this the click is made and\n"
	"                not checked\n"
	"    confirm_ms=<n>\n"
	"                how long to keep looking after each click. Every\n"
	"                attempt gets this in full\n"
	"    retries=<n> further clicks to make when one goes unconfirmed\n"
	"'confirm_ms' and 'retries' default to CONFIRM_TIMEOUT_DEFAULT and\n"
	"ACTION_RETRIES_DEFAULT from the settings file, which is what the\n"
	"programs confirm their own clicks with.\n"
	"'find' and 'refresh' do not apply to clicking a bare point.\n"
	"'confirm_ms' and 'retries' mean nothing without 'confirm'.\n";

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

// "1214,346", or "1214,?" when only one coordinate is known - which is
// what the position cache hands back for a pattern that holds still along
// one axis only.
std::string corner_text(const cv::Point& corner)
{
	const auto axis = [](int value) {
		return value > ImageLibrary::UNKNOWN
			? std::to_string(value) : std::string("?");
	};
	return axis(corner.x) + "," + axis(corner.y);
}

// "a 302x88 box at 3113,300" - where a search actually looked.
std::string box_text(const cv::Rect& box)
{
	std::ostringstream text;
	text << "a " << box.width << "x" << box.height
	     << " box at " << box.x << "," << box.y;
	return text.str();
}

// Where one pattern's share of a search looked, as a phrase to follow
// "searched" or "looked for 'x' in".
std::string scope_text(const PatternSearch& part)
{
	switch (part.scope) {
	case SearchScope::BOX:
		return box_text(part.box);
	case SearchScope::BOX_THEN_FULL:
		return "the whole frame, after " + box_text(part.box) + " missed";
	case SearchScope::FULL:
		return "the whole frame";
	default:
		return {};   // never looked at, see below
	}
}

// "2 candidates were something similar", empty when none were. Worth
// saying out loud: those are the difference between "not there" and
// "not recognised".
std::string mistaken_text(int mistaken)
{
	if (mistaken <= 0) return {};
	return std::to_string(mistaken)
	     + (1 == mistaken ? " candidate was" : " candidates were")
	     + " something similar";
}

// "gate,station" -> the two names. Empty pieces are dropped, so a trailing
// comma is not an error.
std::vector<std::string> split_commas(const std::string& list)
{
	std::vector<std::string> pieces;
	std::istringstream       stream {list};
	std::string              piece;
	while (std::getline(stream, piece, ','))
		if (!piece.empty()) pieces.push_back(piece);
	return pieces;
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
	m_detector.set_min_margine(m_settings.min_margine());
	m_detector.set_cache(&m_positions);
	if (!m_detector.start(m_images, m_capture, error)) {
		std::cout << " [WARNING] Image detection is unavailable: "
		          << error << "\n";
	}
	if (!load_programs()) {
		m_detector.stop();
		return -1;
	}
	load_menu();
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
	// Before the programs, so nothing can be started from the menu while
	// the rest of this is taking itself apart.
	m_menu.stop();
	// A program still working would keep using the detector and the
	// capture we are about to shut down.
	if (m_programs.running()) {
		std::cout << "Waiting for '" << m_programs.current()
		          << "' to stop...\n";
		m_programs.abort();
	}
	m_programs.wait();
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
	else if ("programs" == command)
		cmd_programs();
	else if ("run" == command)
		cmd_run(args);
	else if ("abort" == command)
		cmd_abort();
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
	follow_positions(target);
}

void Cli::follow_positions(const WindowInfo& target)
{
	// Cached positions belong to a frame size, so the frame itself has to
	// say what that is - the window has several plausible rectangles and
	// only one of them is what detections are measured in. The first frame
	// lands a few milliseconds after 'start'.
	Frame frame = m_capture.latest_frame();
	for (int wait = 0; frame.empty() && wait < FRAME_WAIT_STEPS; ++wait) {
		std::this_thread::sleep_for(FRAME_WAIT_STEP);
		frame = m_capture.latest_frame();
	}
	if (frame.empty()) {
		std::cout << " [WARNING] No frame yet, so positions cannot be "
		             "restored or remembered; 'stop' and 'start' again once "
		             "the window is drawing.\n";
		return;
	}

	const std::string character = to_utf8(
		eve_character_name(target.title, m_settings.eve_window().title_prefix)
	);
	const PositionCache::Follow followed = m_positions.follow(
		m_images, character,
		static_cast<int>(frame.width), static_cast<int>(frame.height)
	);
	if (!followed.changed) return;   // same client, nothing moved

	std::cout << "Positions: '" << character << "' at "
	          << frame.width << "x" << frame.height << ", ";
	if (followed.restored > 0)
		std::cout << followed.restored << " restored from "
		          << to_utf8(m_positions.source_path()) << "\n";
	else
		std::cout << "none remembered yet; the first search of each pattern "
		             "scans the whole frame\n";
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
	} else {
		const Frame frame = m_capture.latest_frame();
		std::cout << "Capture: running at " << m_capture.frame_rate()
		          << " fps, " << m_capture.frame_count()
		          << " frames grabbed (" << m_capture.arrived_count()
		          << " delivered by the window).\n";
		if (frame.empty())
			std::cout << "Last frame: none yet.\n";
		else
			std::cout << "Last frame: "
			          << frame.width << "x" << frame.height << "\n";
	}
	// The cache is written from the detection thread, which has no polite
	// way to interrupt the console, so this is where trouble with it shows.
	std::cout << "Positions: " << m_positions.state_text() << "\n";
	std::cout << "Menu: "
	          << (m_menu.running()
	                  ? hotkey_text(m_menu.hotkey())
	                  : std::string("not running"))
	          << "\n";
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
		const cv::Point margines = m_detector.search_margines(pattern);
		std::cout << "  [" << image << "] " << pattern.name
		          << "  " << pattern.width() << "x" << pattern.height()
		          << (pattern.masked() ? ", alpha mask" : ", opaque")
		          << ", threshold " << certainty_text(pattern.threshold)
		          << ", fixed " << fixed_directions_text(pattern.fixed_directions)
		          << ", margines " << margines.x << "x" << margines.y;
		if (ImageLibrary::seen(last))
			std::cout << ", last seen at " << corner_text(last);
		std::cout << "\n";
		if (!pattern.similar.empty()) {
			std::cout << "      similar to";
			for (const size_t twin : pattern.similar)
				std::cout << " " << m_images(twin).name;
			std::cout << "\n";
		}
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
		std::cout << "Usage: detect <image>[,<image>...] [count] [quick]\n";
		cmd_images();
		return;
	}

	// A comma separated list means any of them will do: one search, and
	// every match reported may have come from any pattern in the list.
	std::vector<size_t> images;
	for (const std::string& token : split_commas(args[0])) {
		const size_t image = resolve_image(token);
		if (ImageLibrary::NOT_FOUND == image) return;
		if (images.end() != std::find(images.begin(), images.end(), image)) {
			std::cout << "'" << m_images(image).name
			          << "' is in the list twice.\n";
			return;
		}
		images.push_back(image);
	}
	if (images.empty()) {
		std::cout << "Usage: detect <image>[,<image>...] [count] [quick]\n";
		return;
	}

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

	const std::string what    = m_detector.names_text(images);
	const bool        several = images.size() > 1;
	const auto        started = std::chrono::steady_clock::now();

	Detection found;
	std::string error;
	if (!m_detector.detect(images, max_hits, quick, found, error)) {
		std::cout << "   [ERROR] " << error << "\n";
		return;
	}
	const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - started
	);

	// With one pattern it all fits on the summary line; with several, each
	// was looked for in its own way and gets a line of its own below.
	std::string where, mistaken;
	if (!several) {
		where = ", searched " + scope_text(found.searched.front());
		const std::string claimed =
			mistaken_text(found.searched.front().mistaken);
		if (!claimed.empty()) mistaken = ", " + claimed;
	}

	if (found.hits.empty()) {
		std::cout << what << " not found ("
		          << spent.count() << " ms" << where << mistaken << ")"
		          << (several ? ":" : ".") << "\n";
	} else {
		std::cout << what << " found " << found.hits.size()
		          << (1 == found.hits.size() ? " time (" : " times (")
		          << spent.count() << " ms" << where << mistaken << "):\n";
		for (const DetectionHit& hit : found.hits) {
			std::cout << "  ";
			if (several) std::cout << "'" << m_images(hit.image).name << "' ";
			std::cout << "corner " << hit.at.x << "," << hit.at.y
			          << "  certainty " << certainty_text(hit.certainty);
			if (!m_images(hit.image).similar.empty())
				std::cout << "  fit " << certainty_text(hit.fit);
			std::cout << "\n";
		}
	}

	if (!several) return;
	for (const PatternSearch& part : found.searched) {
		const std::string name = m_images(part.image).name;
		// A pattern the search never reached is not a pattern that was not
		// there: any one of them was all that was asked for, and one of the
		// others answered before this one cost anything.
		if (SearchScope::NONE == part.scope) {
			std::cout << "  did not look for '" << name
			          << "': another pattern answered first\n";
			continue;
		}
		std::cout << "  looked for '" << name << "' in " << scope_text(part);
		const std::string claimed = mistaken_text(part.mistaken);
		if (!claimed.empty()) std::cout << ", " << claimed;
		std::cout << "\n";
	}
}

bool Cli::load_programs()
{
	add_programs(m_programs);

	std::string error;
	if (!m_programs.configure(m_params, error)) {
		std::cout << "   [ERROR] " << error << "\n";
		return false;
	}
	m_programs.on_finish(
		[this](const std::string& name, const ProgramResult& result) {
			report_program(name, result);
		}
	);
	return true;
}

void Cli::load_menu()
{
	if (!m_settings.menu_hotkey().valid()) {
		std::cout << "Menu: off (" << Settings::MENU_HOTKEY_DEFAULT
		          << " by default; set MENU_HOTKEY in "
		          << to_utf8(Settings::FILE_NAME) << " to turn it on).\n";
		return;
	}

	// The menu asks the same questions 'programs', 'run' and 'abort'
	// answer, and gets them answered the same way - it is another way in,
	// not another set of rules.
	MenuHooks hooks;
	hooks.programs = [this] {
		std::vector<std::string> names;
		names.reserve(m_programs.size());
		for (size_t i = 0; i < m_programs.size(); ++i)
			names.push_back(m_programs(i).name());
		return names;
	};
	hooks.running = [this] { return m_programs.current(); };
	hooks.start   = [this](size_t program) { start_from_menu(program); };
	hooks.abort   = [this] { abort_from_menu(); };
	// Whatever is being captured is what a program would act on, so that
	// is what the menu should appear over. Nothing while capture is
	// stopped, even though the handle outlives it.
	hooks.anchor  = [this] {
		return m_capture.running() ? m_capture.target() : nullptr;
	};

	std::string error;
	if (!m_menu.start(m_settings.menu_hotkey(), std::move(hooks), error)) {
		std::cout << " [WARNING] The pop-up menu is unavailable: " << error
		          << "\n           Everything it offers can still be typed.\n";
		return;
	}
	std::cout << "Menu: press " << hotkey_text(m_settings.menu_hotkey())
	          << " for the program list over the captured window.\n";
}

void Cli::start_from_menu(size_t program)
{
	if (program >= m_programs.size()) return;   // the list moved under it

	if (!m_capture.running()) {
		print_note("Capture is not running; 'start' it before running a "
		           "program.");
		return;
	}
	if (m_programs.running()) {
		print_note("'" + m_programs.current() + "' is already running.");
		return;
	}

	const ProgramContext context {
		m_images, m_detector, m_capture, m_settings.program_defaults()
	};
	std::string error;
	if (!m_programs.start(program, context, error)) {
		print_note("   [ERROR] " + error);
		return;
	}
	print_note("Started '" + m_programs(program).name() + "' from the menu.");
}

void Cli::abort_from_menu()
{
	if (!m_programs.running()) {
		print_note("No program is running.");
		return;
	}
	const std::string name = m_programs.current();
	m_programs.abort();
	print_note("Asked '" + name
	           + "' to stop; it will finish the step it is on.");
}

void Cli::report_program(const std::string& name, const ProgramResult& result)
{
	print_note(
		"[" + name + "] " + program_exit_text(result.exit) + ": "
		+ result.description
	);
}

void Cli::print_note(const std::string& text) const
{
	// This runs on somebody else's thread, so the console is most likely
	// sitting at a prompt: start on a fresh line and put the prompt back.
	std::cout << "\n" << text << "\neve> " << std::flush;
}

void Cli::cmd_programs() const
{
	if (m_programs.empty()) {
		std::cout << "No programs are registered.\n";
		return;
	}
	std::cout << "Programs (" << m_programs.size() << "):\n";
	for (size_t i = 0; i < m_programs.size(); ++i) {
		const Program& program = m_programs(i);
		std::cout << "  [" << i << "] " << program.name()
		          << " -- " << program.purpose() << "\n"
		          << "      " << program.settings_text() << "\n";
	}
	std::cout << "Parameters: "
	          << (m_params.loaded()
	                 ? to_utf8(m_params.source_path()) + " (* set there,"
	                   " the rest are built in)"
	                 : std::string("none loaded, using built in defaults"))
	          << "\n";
	if (m_programs.running())
		std::cout << "Running: " << m_programs.current() << "\n";
}

void Cli::cmd_run(const std::vector<std::string>& args)
{
	if (args.empty()) {
		std::cout << "Usage: run <program>\n";
		cmd_programs();
		return;
	}
	// The name is checked before the runner is, so that a typo is reported
	// as a typo rather than as whatever else happens to be going on.
	// Same addressing as images: a name, or the index 'programs' prints.
	size_t program = ProgramRunner::NOT_FOUND;
	int    typed {0};
	if (parse_int(args[0], typed)) {
		if (typed < 0 || static_cast<size_t>(typed) >= m_programs.size()) {
			std::cout << "No program with index " << args[0] << "\n";
			return;
		}
		program = static_cast<size_t>(typed);
	} else {
		program = m_programs.index(args[0]);
		if (ProgramRunner::NOT_FOUND == program) {
			std::cout << "No program called '" << args[0] << "'; there is: "
			          << m_programs.name_list() << "\n";
			return;
		}
	}

	if (m_programs.running()) {
		std::cout << "'" << m_programs.current()
		          << "' is already running; use 'abort' first.\n";
		return;
	}

	const ProgramContext context {
		m_images, m_detector, m_capture, m_settings.program_defaults()
	};
	std::string error;
	if (!m_programs.start(program, context, error)) {
		std::cout << "   [ERROR] " << error << "\n";
		return;
	}
	std::cout << "Started '" << m_programs(program).name()
	          << "'; it will report when it finishes ('abort' stops it).\n";
}

void Cli::cmd_abort()
{
	if (!m_programs.running()) {
		std::cout << "No program is running.\n";
		return;
	}
	const std::string name = m_programs.current();
	m_programs.abort();
	std::cout << "Asked '" << name
	          << "' to stop; it will finish the step it is on.\n";
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

	// The same three numbers the programs click with, so what is tried by
	// hand here behaves the way it will once a program does it.
	const ProgramDefaults& usual = m_settings.program_defaults();

	bool find    {true};
	bool refresh {false};
	int  wait_ms {UI_WAIT_DEFAULT};
	bool chosen  {false};   // find or refresh named explicitly

	// Pattern names are taken from the untouched argument, not the lowered
	// copy the options are matched against: the library is spelt exactly.
	std::vector<std::string> confirm_names;
	int  confirm_ms {static_cast<int>(usual.CONFIRM_TIMEOUT.count())};
	int  retries    {usual.ACTION_RETRIES};
	bool insisted   {false};   // confirm_ms or retries named explicitly

	for (size_t i = first_option; i < args.size(); ++i) {
		const std::string option = to_lower(args[i]);
		if      ("find"    == option) { find    = true;  chosen = true; }
		else if ("nofind"  == option) { find    = false; chosen = true; }
		else if ("refresh" == option) { refresh = true;  chosen = true; }
		else if ("norefresh" == option) { refresh = false; chosen = true; }
		else if (0 == option.compare(0, 8, "wait_ms=")) {
			if (!parse_int(option.substr(8), wait_ms) || wait_ms < 0) {
				std::cout << "Not a wait in milliseconds: " << args[i] << "\n";
				return;
			}
		}
		else if (0 == option.compare(0, 8, "confirm=")) {
			confirm_names = split_commas(args[i].substr(8));
			if (confirm_names.empty()) {
				std::cout << "'confirm=' names no image.\n";
				return;
			}
		}
		else if (0 == option.compare(0, 11, "confirm_ms=")) {
			if (!parse_int(option.substr(11), confirm_ms) || confirm_ms < 1) {
				std::cout << "Not a confirmation timeout in milliseconds: "
				          << args[i] << "\n";
				return;
			}
			insisted = true;
		}
		else if (0 == option.compare(0, 8, "retries=")) {
			if (!parse_int(option.substr(8), retries) || retries < 0) {
				std::cout << "Not a number of retries: " << args[i] << "\n";
				return;
			}
			insisted = true;
		}
		else {
			std::cout << "Unknown click option: " << args[i] << "\n"
			          << CLICK_USAGE;
			return;

		}
	}

	if (point && chosen) {
		std::cout << "'find' and 'refresh' only apply to clicking an image.\n";
		return;
	}
	// Both only say how hard to insist on a confirmation, so neither means
	// anything without something to be confirmed by.
	if (insisted && confirm_names.empty()) {
		std::cout << "'confirm_ms' and 'retries' need 'confirm=<image>' to "
		             "say what the click is supposed to bring about.\n";
		return;
	}

	// Resolved before the click rather than after it, so a mistyped name
	// costs nothing: a click cannot be taken back.
	ClickConfirm confirm;
	confirm.timeout = std::chrono::milliseconds {confirm_ms};
	confirm.retries = retries;
	for (const std::string& token : confirm_names) {
		const size_t image = resolve_image(token);
		if (ImageLibrary::NOT_FOUND == image) return;
		if (confirm.images.end() != std::find(
				confirm.images.begin(), confirm.images.end(), image))
		{
			std::cout << "'" << m_images(image).name
			          << "' is in the confirm list twice.\n";
			return;
		}
		confirm.images.push_back(image);
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
		// when there is nothing to aim at. Half a corner - all the position
		// cache keeps for a pattern fixed along one axis - is not enough:
		// the other coordinate has to be found before anything is clicked.
		if (refresh || !ImageLibrary::located(corner)) {
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

	ProgramContext context {
		m_images, m_detector, m_capture, m_settings.program_defaults()
	};
	ClickReport    report;
	std::string    error;
	const Click outcome = confirmed_click(
		context, target, wait_ms, confirm, StopCheck {}, report, error
	);
	// Nothing was clicked, or the search after it could not run. Either
	// way there is no click to describe.
	if (Click::TROUBLE == outcome && 0 == report.clicks) {
		std::cout << "   [ERROR] " << error << "\n";
		return;
	}

	std::cout << "Clicked " << what
	          << "frame " << report.click.frame.x << "," << report.click.frame.y
	          << " -> screen " << report.click.screen.x << ","
	          << report.click.screen.y;
	if (report.clicks > 1)       std::cout << ", " << report.clicks << " times";
	if (report.click.activated)  std::cout << ", raised the window";
	if (report.click.restored)   std::cout << ", put the focus back";
	if (wait_ms > 0)             std::cout << ", waited " << wait_ms << " ms";
	std::cout << "\n";

	switch (outcome) {
	case Click::CONFIRMED:
		std::cout << "  confirmed by '" << m_images(report.image).name
		          << "' at " << point_text(report.at)
		          << " after " << seconds_text(report.spent) << "\n";
		break;
	case Click::UNCONFIRMED:
		std::cout << "  NOT confirmed: " << error
		          << " (" << seconds_text(report.spent) << ")\n";
		break;
	case Click::TROUBLE:
		std::cout << "   [ERROR] " << error << "\n";
		break;
	default:
		break;   // a plain click, which the line above has already said
	}
}
