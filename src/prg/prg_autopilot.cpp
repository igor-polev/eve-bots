/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram implementation.
*/

#include "prg/prg_autopilot.hpp"

namespace {

// Goes in front of every message, so a failure says which hop it was on.
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
	// The undock program runs as the first step, so it reads its own part
	// of the file just as it would when run alone.
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
	sub_program(m_undock);
	for (int jump = 1; ; ++jump) {
		Doing note {*this, hop_text(jump)};
		if (fly_hop())
			return done("route finished over " + hop_text(jump));
	}
}

size_t AutopilotProgram::next_waypoint(const Budget& reading)
{
	Sighting seen;
	while (reading.left() > eb::Millis::zero()) {
		if (visible(GATE))           return GATE;
		if (visible(STATIONS, seen)) return seen.image;
		pause(RECHECK_INTERVAL, "the route panel to say what is next");
	}
	fail("nothing in the OV panel to fly to");
}

bool AutopilotProgram::fly_hop()
{
	const Budget dest_to {DESTINATION_TIMEOUT};

	const size_t   next       {next_waypoint(dest_to)};
	const bool     to_station {GATE.index() != next};
	const Pattern& command    {to_station ? DOCK : JUMP};

	click(next,    dest_to, {command});
	click(command, dest_to, {WARP_VECTOR, WARP});

	const Budget warp_to {WARP_TIMEOUT};
	appear(WARP, warp_to);
	vanish(WARP, warp_to, RECHECK_INTERVAL);

	pause(to_station ? DOCKING_PAUSE : GATE_JUMP_PAUSE,
	      to_station ? "the ship to dock" : "the gate to load");
	if (!to_station) return false;

	appear(UNDOCK, DOCKING_TIMEOUT);
	return true;
}
