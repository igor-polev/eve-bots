/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram implementation.
*/

#include "autopilot_program.hpp"

namespace {

using Clock = ProgramClock;

// Reported in every message so a failure says which hop it happened on.
std::string hop_text(int hop)
{
	return "hop " + std::to_string(hop);
}

} // namespace

std::string AutopilotProgram::purpose() const
{
	return "undock if needed, then fly the set route until the ship docks";
}

std::string AutopilotProgram::settings_text() const
{
	return std::string(KEY_DESTINATION_TIMEOUT) + " "
	     + std::to_string(m_destination_timeout.count()) + " ms, "
	     + KEY_CMD_SEARCH_TIMEOUT + " "
	     + std::to_string(m_cmd_search_timeout.count()) + " ms, "
	     + KEY_ENTER_WARP_TIMEOUT + " "
	     + std::to_string(m_enter_warp_timeout.count()) + " ms,\n      "
	     + KEY_WARP_RECHECK_PAUSE + " "
	     + std::to_string(m_warp_recheck_pause.count()) + " ms, "
	     + KEY_MAX_JUMP_TIMEOUT + " "
	     + std::to_string(m_max_jump_timeout.count()) + " ms,\n      "
	     + KEY_GATE_JUMP_PAUSE + " "
	     + std::to_string(m_gate_jump_pause.count()) + " ms, "
	     + KEY_DOCKING_PAUSE + " "
	     + std::to_string(m_docking_pause.count()) + " ms, "
	     + KEY_DOCKING_TIMEOUT + " "
	     + std::to_string(m_docking_timeout.count()) + " ms";
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

	struct Setting {
		const char*                key;
		int                        fallback;
		std::chrono::milliseconds* target;
	};
	const Setting settings[] = {
		{KEY_DESTINATION_TIMEOUT, DESTINATION_TIMEOUT_DEFAULT, &m_destination_timeout},
		{KEY_CMD_SEARCH_TIMEOUT,  CMD_SEARCH_TIMEOUT_DEFAULT,  &m_cmd_search_timeout},
		{KEY_ENTER_WARP_TIMEOUT,  ENTER_WARP_TIMEOUT_DEFAULT,  &m_enter_warp_timeout},
		{KEY_WARP_RECHECK_PAUSE,  WARP_RECHECK_PAUSE_DEFAULT,  &m_warp_recheck_pause},
		{KEY_MAX_JUMP_TIMEOUT,    MAX_JUMP_TIMEOUT_DEFAULT,    &m_max_jump_timeout},
		{KEY_GATE_JUMP_PAUSE,     GATE_JUMP_PAUSE_DEFAULT,     &m_gate_jump_pause},
		{KEY_DOCKING_PAUSE,       DOCKING_PAUSE_DEFAULT,       &m_docking_pause},
		{KEY_DOCKING_TIMEOUT,     DOCKING_TIMEOUT_DEFAULT,     &m_docking_timeout},
	};

	for (const Setting& setting : settings) {
		const double value =
			params.number(name(), setting.key, setting.fallback);
		// Zero would give a search no chance to run even once, and a
		// negative pause is meaningless.
		if (value <= 0.0) {
			error = std::string(setting.key)
			      + " is milliseconds and must be greater than zero";
			return false;
		}
		*setting.target =
			std::chrono::milliseconds {static_cast<long long>(value)};
	}
	return true;
}

void AutopilotProgram::request_stop() noexcept
{
	Program::request_stop();
	m_undock.request_stop();
}

ProgramResult AutopilotProgram::run(ProgramContext& context)
{
	const Clock::time_point started = Clock::now();

	if (!context.capture.running()) {
		return {ProgramExit::FAILURE,
		        "capture is not running; use 'start' first"};
	}

	// Every pattern is resolved up front, so a missing one is reported
	// before the ship is committed to anything.
	Waypoints images;
	size_t    station {0}, station_home {0};
	const std::pair<const char*, size_t*> needed[] = {
		{GATE_IMAGE,         &images.gate},
		{STATION_IMAGE,      &station},
		{STATION_HOME_IMAGE, &station_home},
		{JUMP_IMAGE,         &images.jump},
		{DOCK_IMAGE,         &images.dock},
		{WARP_IMAGE,         &images.warp},
		{UNDOCK_IMAGE,       &images.undock},
	};
	std::string missing;
	for (const auto& want : needed) {
		const size_t at = context.images.index(want.first);
		if (ImageLibrary::NOT_FOUND == at) {
			if (!missing.empty()) missing += ", ";
			missing += want.first;
		}
		*want.second = at;
	}
	if (!missing.empty()) {
		return {ProgramExit::FAILURE,
		        "the image library has no pattern called " + missing};
	}
	images.stations = {station, station_home};

	// 1. Get into space first. Undock succeeds without doing anything if
	//    the ship is already out, so this costs one search when it is.
	const ProgramResult undocked = m_undock.exec(context);
	if (ProgramExit::SUCCESS != undocked.exit) {
		return {undocked.exit,
		        "cannot start: the undock step " + undocked.description};
	}

	// 2. One pass per hop until the route runs out at a station.
	for (int hop = 1; ; ++hop) {
		bool done {false};
		const ProgramResult flown = fly_hop(context, images, hop, done);
		if (ProgramExit::SUCCESS != flown.exit) return flown;
		if (done) {
			return {ProgramExit::SUCCESS,
			        "route finished in " + seconds_text(since(started))
			        + " over " + std::to_string(hop)
			        + (1 == hop ? " hop: " : " hops: ")
			        + flown.description};
		}
	}
}

ProgramResult AutopilotProgram::fly_hop(
	ProgramContext&  context,
	const Waypoints& images,
	int              hop,
	bool&            done)
{
	const Clock::time_point started = Clock::now();
	done = false;

	// Which waypoint is next decides everything that follows, so it is
	// settled once here and carried through the rest of the hop.
	const Clock::time_point deadline = started + m_destination_timeout;
	cv::Point   corner;
	std::string trouble;
	bool        to_station {false};
	// Which pattern the icon turned out to be. The click below is aimed at
	// its middle, and the station icons are not all the same size, so this
	// has to be the one that actually matched rather than the one asked for.
	size_t      marker {images.gate};

	while (true) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - Clock::now()
		);
		if (left <= std::chrono::milliseconds::zero()) {
			return {ProgramExit::FAILURE,
			        hop_text(hop) + ": no gate or station in the route panel "
			        "within " + seconds_text(m_destination_timeout)
			        + " - is a destination still set?"};
		}

		// The gate first, because it is the common case by far.
		Look seen = look_once(context, images.gate, left, corner, trouble);
		if (Look::MISSING == seen) {
			// Either station icon will do, so both go into one search
			// rather than costing a pass each.
			seen = look_once(
				context, images.stations, left, corner, marker, trouble
			);
			to_station = Look::FOUND == seen;
		}
		if (Look::FOUND == seen) break;
		if (Look::STOPPED == seen) {
			return {ProgramExit::STOPPED,
			        hop_text(hop) + ": stopped while reading the route"};
		}
		if (Look::TROUBLE == seen) {
			return {ProgramExit::FAILURE,
			        hop_text(hop) + ": cannot read the route: " + trouble};
		}
		if (!wait(m_warp_recheck_pause)) {
			return {ProgramExit::STOPPED,
			        hop_text(hop) + ": stopped while reading the route"};
		}
	}

	const char* const where   = to_station ? "station" : "gate";
	const size_t      command = to_station ? images.dock : images.jump;
	const char* const command_name = to_station ? DOCK_IMAGE : JUMP_IMAGE;

	// 3. Click the waypoint, which selects it.
	ClickResult clicked;
	if (!click_middle(context, marker, corner, clicked, trouble)) {
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": cannot click the " + where
		        + " at " + point_text(corner) + ": " + trouble};
	}

	// 4. Selecting it brings up the command button for that kind of hop.
	switch (look_for(context, command, m_cmd_search_timeout, corner, trouble)) {
	case Look::FOUND:
		break;
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        hop_text(hop) + ": stopped while waiting for the '"
		        + command_name + "' button"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": cannot look for the '" + command_name
		        + "' button: " + trouble};
	default:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": no '" + command_name + "' button within "
		        + seconds_text(m_cmd_search_timeout)
		        + " after selecting the " + where
		        + " - did the click select anything?"};
	}

	// 5. Press it.
	if (!click_middle(context, command, corner, clicked, trouble)) {
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": cannot click the '" + command_name
		        + "' button at " + point_text(corner) + ": " + trouble};
	}

	// 6. The warp message appearing is the proof the command was taken.
	cv::Point warping;
	switch (look_for(context, images.warp, m_enter_warp_timeout,
	                 warping, trouble))
	{
	case Look::FOUND:
		break;
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        hop_text(hop) + ": stopped while waiting for warp to start"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": cannot look for the warp message: "
		        + trouble};
	default:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": warp did not start within "
		        + seconds_text(m_enter_warp_timeout) + " of pressing '"
		        + command_name + "'"};
	}

	// 7. ...and it going away again is the proof the ship arrived. Only
	//    one look per pause: this waits out a whole warp, and a full frame
	//    search on repeat would keep a core busy for minutes.
	const Clock::time_point warped = Clock::now();
	while (true) {
		if (!wait(m_warp_recheck_pause)) {
			return {ProgramExit::STOPPED,
			        hop_text(hop) + ": stopped in warp"};
		}
		const auto left = m_max_jump_timeout - since(warped);
		if (left <= std::chrono::milliseconds::zero()) {
			return {ProgramExit::FAILURE,
			        hop_text(hop) + ": still in warp after "
			        + seconds_text(m_max_jump_timeout)};
		}

		const Look seen = look_once(context, images.warp, left, warping, trouble);
		if (Look::MISSING == seen) break;          // arrived
		if (Look::STOPPED == seen) {
			return {ProgramExit::STOPPED,
			        hop_text(hop) + ": stopped in warp"};
		}
		if (Look::TROUBLE == seen) {
			return {ProgramExit::FAILURE,
			        hop_text(hop) + ": cannot watch the warp message: "
			        + trouble};
		}
	}

	// 8. Arriving is not the same as being through: the gate still has to
	//    load the next system, and docking still has to play out.
	if (!wait(to_station ? m_docking_pause : m_gate_jump_pause)) {
		return {ProgramExit::STOPPED,
		        hop_text(hop) + std::string(": stopped while the ")
		        + (to_station ? "ship docked" : "gate loaded")};
	}

	if (!to_station) return {ProgramExit::SUCCESS, "gate jumped"};

	// 9. A station hop is the last one, and the undock button coming back
	//    is the proof the ship really is inside.
	cv::Point button;
	switch (look_for(context, images.undock, m_docking_timeout,
	                 button, trouble))
	{
	case Look::FOUND:
		done = true;
		return {ProgramExit::SUCCESS,
		        "docked, undock button back at " + point_text(button)
		        + " after " + seconds_text(since(started))};
	case Look::STOPPED:
		return {ProgramExit::STOPPED,
		        hop_text(hop) + ": stopped while waiting to dock"};
	case Look::TROUBLE:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": cannot look for the undock button: "
		        + trouble};
	default:
		return {ProgramExit::FAILURE,
		        hop_text(hop) + ": no undock button within "
		        + seconds_text(m_docking_timeout)
		        + " of arriving at the station - did docking finish?"};
	}
}
