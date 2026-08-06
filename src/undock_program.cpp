/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram implementation.
*/

#include "undock_program.hpp"

namespace {

// Given to the game between clicking undock and looking for the result.
// The undock animation is long, so this is only meant to cover the click
// being processed at all; the retries cover the rest.
constexpr std::chrono::milliseconds AFTER_CLICK {1000};

using Clock = ProgramClock;

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
	     + std::to_string(m_undock_timeout.count()) + " ms";
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

	m_search_timeout =
		std::chrono::milliseconds {static_cast<long long>(search)};
	m_undock_timeout =
		std::chrono::milliseconds {static_cast<long long>(undock)};
	return true;
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

	// Everything up to the click shares one deadline, so a client showing
	// nothing recognisable still gives up on time.
	const Clock::time_point deadline = started + m_search_timeout;
	const auto time_left = [deadline] {
		const auto to_go = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - Clock::now()
		);
		return to_go > std::chrono::milliseconds::zero()
			? to_go : std::chrono::milliseconds::zero();
	};

	// 1. We are supposed to be docked, so look for the undock button once.
	cv::Point   corner;
	std::string trouble;
	Look seen = look_once(context, undock, time_left(), corner, trouble);

	if (Look::MISSING == seen) {
		// 2. Not there. Far more often that means the ship was never
		//    docked than that the button is late, and one look at the
		//    ship core settles it without spending the whole budget
		//    retrying a button that is never going to appear.
		cv::Point core;
		switch (look_once(context, shipcore, time_left(), core, trouble)) {
		case Look::FOUND:
			return {ProgramExit::SUCCESS,
			        "already in space after " + seconds_text(since(started))
			        + ": no undock button, ship core at " + point_text(core)};
		case Look::STOPPED:
			return {ProgramExit::STOPPED,
			        "stopped while checking whether the ship was already "
			        "in space"};
		case Look::TROUBLE:
			return {ProgramExit::FAILURE,
			        "no undock button, and cannot look for the ship core: "
			        + trouble};
		default:
			break;
		}

		// 3. Neither pattern is on screen. Now late drawing is the likely
		//    explanation, so go back to the button and keep trying.
		seen = look_for(context, undock, time_left(), corner, trouble);
	}

	switch (seen) {
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        "stopped while looking for the undock button"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        "cannot look for the undock button: " + trouble};
	case Look::MISSING:
		return {ProgramExit::FAILURE,
		        "neither the undock button nor the ship core within "
		        + seconds_text(m_search_timeout)
		        + " - is the client on a loading screen, or are the "
		          "patterns cut at a different resolution?"};
	default:
		break;
	}

	// 2. Click the middle of it.
	ClickResult clicked;
	if (!click_middle(context, undock, corner, clicked, trouble)) {
		return {ProgramExit::FAILURE,
		        "cannot click the undock button at " + point_text(corner)
		        + ": " + trouble};
	}
	const cv::Point target = clicked.frame;
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
		        + " (screen " + point_text(clicked.screen)
		        + "), but no ship core within "
		        + seconds_text(m_undock_timeout)
		        + " - did the click register?"};
	default:
		break;
	}

	return {ProgramExit::SUCCESS,
	        "undocked in " + seconds_text(since(started))
	        + ": clicked " + point_text(target)
	        + ", ship core at " + point_text(core)
	        + " after " + seconds_text(since(pressed))};
}
