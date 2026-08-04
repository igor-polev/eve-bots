/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram implementation.
*/

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "undock_program.hpp"

namespace {

// Pause between two attempts at the same pattern. A full frame search
// already costs seconds, so this only matters once a quick box search is
// possible - and then it keeps the program from spinning on the CPU.
constexpr std::chrono::milliseconds RETRY_PAUSE {250};

// Given to the game between clicking undock and looking for the result.
// The undock animation is long, so this is only meant to cover the click
// being processed at all; the retries cover the rest.
constexpr std::chrono::milliseconds AFTER_CLICK {1000};

using Clock = std::chrono::steady_clock;

std::chrono::milliseconds since(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		Clock::now() - start
	);
}

// "4.2 s"
std::string seconds_text(std::chrono::milliseconds spent)
{
	std::ostringstream text;
	text << std::fixed << std::setprecision(1)
	     << (spent.count() / 1000.0) << " s";
	return text.str();
}

std::string point_text(const cv::Point& at)
{
	return std::to_string(at.x) + "," + std::to_string(at.y);
}

std::string to_lower(std::string text)
{
	std::transform(
		text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(::tolower(c)); }
	);
	return text;
}

} // namespace

std::string UndockProgram::purpose() const
{
	return "leave the station and confirm the ship reached space";
}

std::string UndockProgram::settings_text() const
{
	return std::string(KEY_SEARCH_TIMEOUT) + " "
	     + std::to_string(m_search_timeout.count()) + " ms, "
	     + KEY_UNDOCK_TIMEOUT + " "
	     + std::to_string(m_undock_timeout.count()) + " ms, "
	     + KEY_CLICK_METHOD + " " + click_method_text(m_click);
}

bool UndockProgram::configure(const ProgramParams& params, std::string& error)
{
	const double search =
		params.number(name(), KEY_SEARCH_TIMEOUT, SEARCH_TIMEOUT_DEFAULT);
	const double undock =
		params.number(name(), KEY_UNDOCK_TIMEOUT, UNDOCK_TIMEOUT_DEFAULT);
	// A zero timeout would give the search no chance to run even once.
	if (search <= 0.0 || undock <= 0.0) {
		error = std::string(KEY_SEARCH_TIMEOUT) + " and " + KEY_UNDOCK_TIMEOUT
		      + " are milliseconds and must be greater than zero";
		return false;
	}

	const std::string method =
		to_lower(params.text(name(), KEY_CLICK_METHOD, "auto"));
	if      ("auto" == method) m_click = ClickMethod::AUTO;
	else if ("send" == method) m_click = ClickMethod::SEND;
	else if ("post" == method) m_click = ClickMethod::POST;
	else {
		error = std::string(KEY_CLICK_METHOD)
		      + " must be \"auto\", \"send\" or \"post\"";
		return false;
	}

	m_search_timeout =
		std::chrono::milliseconds {static_cast<long long>(search)};
	m_undock_timeout =
		std::chrono::milliseconds {static_cast<long long>(undock)};
	return true;
}

UndockProgram::Look UndockProgram::look_for(
	ProgramContext&           context,
	size_t                    image,
	std::chrono::milliseconds budget,
	cv::Point&                corner,
	std::string&              trouble) const
{
	const Clock::time_point deadline = Clock::now() + budget;
	while (true) {
		if (stopping()) return Look::STOPPED;

		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - Clock::now()
		);
		if (left <= std::chrono::milliseconds::zero()) return Look::MISSING;

		// The search gets what is left of the budget and no more, so a
		// single slow full frame pass cannot overrun the whole timeout.
		Detection found;
		if (!context.detector.detect(image, 1, false, found, trouble, left)) {
			// Running out of time is not a fault, it is the answer.
			if (Clock::now() >= deadline) return Look::MISSING;
			return Look::TROUBLE;
		}
		if (!found.hits.empty()) {
			corner = found.hits.front().at;
			return Look::FOUND;
		}
		if (!wait(RETRY_PAUSE)) return Look::STOPPED;
	}
}

ProgramResult UndockProgram::run(ProgramContext& context)
{
	const Clock::time_point started = Clock::now();

	if (!context.capture.running()) {
		return {ProgramExit::FAILURE,
		        "capture is not running; use 'start' first"};
	}
	const size_t undock   = context.images.index(UNDOCK_IMAGE);
	const size_t shipcore = context.images.index(SHIPCORE_IMAGE);
	if (ImageLibrary::NOT_FOUND == undock ||
		ImageLibrary::NOT_FOUND == shipcore)
	{
		return {ProgramExit::FAILURE,
		        std::string("the image library needs patterns called '")
		        + UNDOCK_IMAGE + "' and '" + SHIPCORE_IMAGE + "'"};
	}

	// 1. The undock button is what says the ship is still docked.
	cv::Point   corner;
	std::string trouble;
	switch (look_for(context, undock, m_search_timeout, corner, trouble)) {
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        "stopped while looking for the undock button"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        "cannot look for the undock button: " + trouble};
	case Look::MISSING:
		return {ProgramExit::FAILURE,
		        "no undock button within " + seconds_text(m_search_timeout)
		        + " - is the ship already in space?"};
	default:
		break;
	}

	// 2. Click the middle of it.
	const ImagePattern& button = context.images(undock);
	const cv::Point target =
		corner + cv::Point {button.width() / 2, button.height() / 2};

	ClickResult clicked;
	if (!click_at(context.capture.target(), target, m_click,
	              UI_WAIT_DEFAULT, clicked, trouble))
	{
		return {ProgramExit::FAILURE,
		        "cannot click the undock button at " + point_text(target)
		        + ": " + trouble};
	}
	const Clock::time_point pressed = Clock::now();
	if (!wait(AFTER_CLICK))
		return {ProgramExit::STOPPED, "stopped just after clicking undock"};

	// 3. The ship core is what says the ship made it out. Whatever the
	//    click and the pause already used comes off the budget.
	const auto left = m_undock_timeout - since(pressed);
	cv::Point  core;
	switch (look_for(context, shipcore, left, core, trouble)) {
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        "clicked undock, then stopped while waiting for space"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        "clicked undock, but cannot look for the ship core: "
		        + trouble};
	case Look::MISSING:
		return {ProgramExit::FAILURE,
		        "clicked undock at " + point_text(target)
		        + " by " + click_method_text(clicked.method)
		        + ", but no ship core within "
		        + seconds_text(m_undock_timeout)
		        + " - did the click register?"};
	default:
		break;
	}

	return {ProgramExit::SUCCESS,
	        "undocked in " + seconds_text(since(started))
	        + ": clicked " + point_text(target)
	        + " by " + click_method_text(clicked.method)
	        + ", ship core at " + point_text(core)
	        + " after " + seconds_text(since(pressed))};
}
