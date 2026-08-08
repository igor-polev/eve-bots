/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram implementation.
*/

#include "prg/prg_autopilot.hpp"

namespace {

// Put in front of every message so a failure says which hop it happened
// on.
std::string hop_text(int hop)
{
	return "hop " + std::to_string(hop);
}

} // namespace

std::string AutopilotProgram::purpose() const
{
	return "undock if needed, then fly the set route until the ship docks";
}

bool AutopilotProgram::configure(
	const ProgramParams& params, std::string& error)
{
	// The undock program runs as the first step, so it reads its own
	// section of the file exactly as it would on its own.
	if (!m_undock.configure(params, error)) {
		error = "the undock step it starts with rejected its settings: " + error;
		return false;
	}
	return Program::configure(params, error);
}

void AutopilotProgram::request_stop() noexcept
{
	Program::request_stop();
	m_undock.request_stop();
}

ProgramResult AutopilotProgram::run()
{
	// Get into space first. Undock succeeds without doing anything if the
	// ship is already out, so this costs one search when it is.
	const ProgramResult undocked = m_undock.exec(context());
	if (ProgramExit::SUCCESS != undocked.exit) {
		throw ProgramError {
			undocked.exit,
			"cannot start: the undock step " + undocked.description
		};
	}

	// Then one pass per hop until the route runs out at a station.
	for (int hop = 1; ; ++hop) {
		Doing note {*this, hop_text(hop)};
		if (fly_hop()) {
			return done(
				"route finished in " + seconds_text(elapsed()) + " over "
				+ std::to_string(hop) + (1 == hop ? " hop" : " hops")
			);
		}
	}
}

Program::Sighting AutopilotProgram::next_waypoint()
{
	const Budget reading {DESTINATION_TIMEOUT};
	while (true) {
		Sighting seen;
		// The gate first, because it is the common case by far.
		if (sighted("the route panel", GATE, reading.left(), seen.at)) {
			seen.image = GATE;
			return seen;
		}
		// Either station icon will do, so both go into one search rather
		// than costing a pass each.
		if (sighted("the route panel", STATIONS, reading.left(), seen))
			return seen;

		if (reading.left() <= std::chrono::milliseconds::zero()) {
			fail("no gate or station in the route panel within "
			     + seconds_text(reading.total())
			     + " - is a destination still set?");
		}
		pause(RECHECK_INTERVAL, "the route panel to say what is next");
	}
}

bool AutopilotProgram::fly_hop()
{
	// Which waypoint is next decides everything that follows: which
	// command to give, how long to wait once warp ends, and whether
	// arriving means the route is finished.
	const Sighting next = next_waypoint();

	const bool        to_station = GATE.index() != next.image;
	const std::string where      = to_station ? "the station" : "the gate";
	const Pattern&    command    = to_station ? DOCK : JUMP;
	const std::string press =
		std::string("the '") + command.name() + "' button";

	// Selecting the waypoint brings up the command button for that kind of
	// hop, so that button is both the proof the click was taken and the
	// thing the next step has to press.
	const cv::Point button = click(where, next.image, next.at, {command});
	// And pressing it has to put the warp vector message on screen, which
	// is the difference between a command taken and one swallowed.
	click(press, command, button, {WARP_VECTOR});

	// Warp is watched from both sides on one budget: it has to start,
	// which says the ship really is on its way, and then end, which says
	// it arrived.
	const Budget warp {WARP_TIMEOUT};
	appear("the warp message", WARP, warp);
	vanish("the warp message", WARP, warp, RECHECK_INTERVAL);

	// Arriving is not the same as being through: the gate still has to
	// load the next system, and docking still has to play out.
	pause(to_station ? DOCKING_PAUSE : GATE_JUMP_PAUSE,
	      to_station ? "the ship to dock" : "the gate to load");
	if (!to_station) return false;

	// A station hop is the last one, and the undock button coming back is
	// the proof the ship really is inside.
	appear("the undock button", UNDOCK, DOCKING_TIMEOUT);
	return true;
}
