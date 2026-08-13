/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram - fly the set route until the ship docks.

	The route panel shows one icon for the next waypoint - a gate for a
	jump, a station for the destination - and which of the two it is decides
	the whole of the rest of the pass. One pass through the loop is one hop:

	    route icon -> click -> jump or dock button -> click
	    -> warp starts -> warp ends -> settle -> next hop

	Both clicks are confirmed by whatever they were supposed to produce, so
	neither needs a search of its own afterwards.

	Warp is watched from both sides on one WARP_TIMEOUT budget: the message
	has to appear, which says the ship is on its way, and then go away,
	which says it arrived. Watching only for its arrival would read a warp
	that never started as success, and one budget rather than two because a
	warp that starts late leaves less time to finish.

	The loop ends when a station hop finishes and the undock button comes
	back, that being the proof the ship is docked.
*/

#pragma once

#include "prg/prg_undock.hpp"
#include "program.hpp"

class AutopilotProgram : public Program {
public:
	AutopilotProgram() : Program("autopilot") {}

	std::string purpose() const override;
	// Also hands the file to the undock program it starts with.
	bool configure(
		const ProgramParams& params, std::string& error) override;

	// Also stops the undock program this one runs first, which would
	// otherwise keep working after the autopilot was asked to stop.
	void request_stop() noexcept override;

protected:
	ProgramResult run() override;

private:
	// One hop. True when that hop was the last one and the ship is docked.
	bool fly_hop();
	// Which icon the route panel says is next, waited for until it says
	// something. That is the whole of the difference between the two kinds
	// of hop; where it sits is the click's business, not ours.
	size_t next_waypoint();

	// What this program works with, named as eve_images.json names it.
	Pattern GATE {*this, "gate_route"};
	// A station is drawn with one of two icons depending on whether it is
	// the home station, and the difference means nothing to flying a
	// route, so either one will do.
	Patterns STATIONS {*this, {"station_route", "station_home_route"}};

	Pattern JUMP {*this, "jump"};
	Pattern DOCK {*this, "dock"};
	Pattern WARP {*this, "warp"};
	// Proof that a jump or dock command was taken: the ship has a warp
	// vector before it has a warp.
	Pattern WARP_VECTOR {*this, "warp_vector"};
	Pattern UNDOCK      {*this, "undock"};

	UndockProgram m_undock;

	// What can be tuned in prog_params.json. The two pauses are the whole of
	// what a hop spends on purpose, so they are where a route gets faster.
	PROG_PARAM(Timeout,  DESTINATION_TIMEOUT, 10000);
	PROG_PARAM(Timeout,  WARP_TIMEOUT,       300000);
	// The gap between two looks at the same thing: the route panel while
	// it is empty, and the warp message while the ship is in warp.
	PROG_PARAM(Interval, RECHECK_INTERVAL,     2000);
	PROG_PARAM(Pause,    GATE_JUMP_PAUSE,      5000);
	PROG_PARAM(Pause,    DOCKING_PAUSE,       10000);
	PROG_PARAM(Timeout,  DOCKING_TIMEOUT,     40000);
};
