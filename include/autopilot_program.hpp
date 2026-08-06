/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram - fly the set route until the ship docks.

	The route panel shows one icon for the next waypoint: a gate when the
	next hop is a jump, a station when it is the destination. Which of the
	two is on screen decides the whole of the rest of the pass - which
	command button to press afterwards, how long to wait once warp ends,
	and whether arriving means the route is finished.

	One pass through the loop is one hop:

	    route icon -> click -> jump or dock button -> click
	    -> warp starts -> warp ends -> settle -> next hop

	Warp is watched from both sides. First the warp message has to appear,
	which says the command was accepted; then it has to go away, which
	says the ship arrived. Watching only for its arrival would read a warp
	that never started as success.

	The loop ends when a station hop finishes and the undock button comes
	back, because that button is the proof the ship is docked. A route
	that ends anywhere else - a gate, an empty route panel - runs out of
	DESTINATION_TIMEOUT and reports failure, since this program has no
	other way to tell "arrived" from "the interface is late".
*/

#pragma once
#include <chrono>

#include "program.hpp"
#include "undock_program.hpp"

class AutopilotProgram : public Program {
public:
	// Patterns this program looks for, by their name in eve_images.json.
	static constexpr const char* GATE_IMAGE    = "gate_route";
	static constexpr const char* STATION_IMAGE = "station_route";
	static constexpr const char* JUMP_IMAGE    = "jump";
	static constexpr const char* DOCK_IMAGE    = "dock";
	static constexpr const char* WARP_IMAGE    = "warp";
	static constexpr const char* UNDOCK_IMAGE  = "undock";

	// Parameter names in prog_params.json, and what they mean without it.
	static constexpr const char* KEY_DESTINATION_TIMEOUT = "DESTINATION_TIMEOUT";
	static constexpr const char* KEY_CMD_SEARCH_TIMEOUT  = "CMD_SEARCH_TIMEOUT";
	static constexpr const char* KEY_ENTER_WARP_TIMEOUT  = "ENTER_WARP_TIMEOUT";
	static constexpr const char* KEY_WARP_RECHECK_PAUSE  = "WARP_RECHECK_PAUSE";
	static constexpr const char* KEY_MAX_JUMP_TIMEOUT    = "MAX_JUMP_TIMEOUT";
	static constexpr const char* KEY_GATE_JUMP_PAUSE     = "GATE_JUMP_PAUSE";
	static constexpr const char* KEY_DOCKING_PAUSE       = "DOCKING_PAUSE";
	static constexpr const char* KEY_DOCKING_TIMEOUT     = "DOCKING_TIMEOUT";

	static constexpr int DESTINATION_TIMEOUT_DEFAULT =  30000;   // ms
	static constexpr int CMD_SEARCH_TIMEOUT_DEFAULT  =  15000;
	static constexpr int ENTER_WARP_TIMEOUT_DEFAULT  =  30000;
	static constexpr int WARP_RECHECK_PAUSE_DEFAULT  =   2000;
	static constexpr int MAX_JUMP_TIMEOUT_DEFAULT    = 300000;
	static constexpr int GATE_JUMP_PAUSE_DEFAULT     =  15000;
	static constexpr int DOCKING_PAUSE_DEFAULT       =  20000;
	static constexpr int DOCKING_TIMEOUT_DEFAULT     =  60000;

	AutopilotProgram() : Program("autopilot") {}

	std::string purpose()       const override;
	std::string settings_text() const override;
	bool        configure(
		const ProgramParams& params, std::string& error) override;

	// Also stops the undock program this one runs first, which would
	// otherwise keep working after the autopilot was asked to stop.
	void request_stop() noexcept override;

protected:
	ProgramResult run(ProgramContext& context) override;

private:
	// Everything one hop needs to know, resolved once at the start.
	struct Waypoints {
		size_t gate {0}, station {0}, jump {0}, dock {0};
		size_t warp {0}, undock {0};
	};

	// One hop. done is set when the route is finished and the ship docked.
	ProgramResult fly_hop(
		ProgramContext&  context,
		const Waypoints& images,
		int              hop,
		bool&            done
	);

	UndockProgram m_undock;

	std::chrono::milliseconds m_destination_timeout {DESTINATION_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_cmd_search_timeout  {CMD_SEARCH_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_enter_warp_timeout  {ENTER_WARP_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_warp_recheck_pause  {WARP_RECHECK_PAUSE_DEFAULT};
	std::chrono::milliseconds m_max_jump_timeout    {MAX_JUMP_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_gate_jump_pause     {GATE_JUMP_PAUSE_DEFAULT};
	std::chrono::milliseconds m_docking_pause       {DOCKING_PAUSE_DEFAULT};
	std::chrono::milliseconds m_docking_timeout     {DOCKING_TIMEOUT_DEFAULT};
};
