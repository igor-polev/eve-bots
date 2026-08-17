/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram - fly the set route until the ship docks.

	The route panel shows one icon for the next waypoint: a gate for a jump,
	a station for the destination. Which of the two it is decides everything
	that follows. One pass through the loop is one hop:

	    route icon -> click -> jump or dock button -> click
	    -> warp starts -> warp ends -> settle -> next hop

	Each click is confirmed by what it was supposed to produce, so no click
	needs a search of its own afterwards.

	Warp is watched from both sides on one WARP_TIMEOUT budget. The warp
	message must appear, which says the ship is on its way, and then go
	away, which says it arrived. Watching only for its arrival would count a
	warp that never started as success. One budget and not two, because a
	warp that starts late has less time left to finish.

	The loop ends when a station hop finishes and the undock button comes
	back, because that button proves the ship is docked.
*/

#pragma once

#include "prg/prg_undock.hpp"
#include "program.hpp"

class AutopilotProgram : public Program {
public:
	AutopilotProgram() : Program("autopilot") {}

	std::string purpose() const override;
	// Also gives the file to the undock program it starts with.
	bool configure(
		const ProgramParams& params, std::string& error) override;

	// Also stops the undock program this one runs first. Without that it
	// would keep working after the autopilot was asked to stop.
	void request_stop() noexcept override;

protected:
	ProgramResult run() override;

private:
	// One hop. True when that hop was the last one and the ship is docked.
	bool fly_hop();
	// Which icon the route panel shows next. It waits until the panel shows
	// something. That icon is the only difference between the two kinds of
	// hop. Where it sits is the click's business, not ours.
	//
	// The budget belongs to the hop, not to this step, and what is left of
	// it goes on to the clicks. A panel that was slow to draw leaves them
	// less time, and that is the point of sharing one budget.
	size_t next_waypoint(const Budget& reading);

	// The images this program uses, named as eve_images.json names them.
	Pattern GATE {*this, "gate_route"};
	// A station has one of two icons, depending on whether it is the home
	// station. That difference does not matter when flying a route, so
	// either icon will do.
	Patterns STATIONS {*this, {"station_route", "station_home_route"}};

	Pattern JUMP {*this, "jump"};
	Pattern DOCK {*this, "dock"};
	Pattern WARP {*this, "warp"};
	// Proof that a jump or dock command was taken: the ship gets a warp
	// vector before the warp itself.
	Pattern WARP_VECTOR {*this, "warp_vector"};
	Pattern UNDOCK      {*this, "undock"};

	UndockProgram m_undock;

	// What prg_params.json can change. The two pauses are all the time a hop
	// spends on purpose, so they are where a route gets faster.
	PROG_PARAM(Timeout,  DESTINATION_TIMEOUT, 10000);
	PROG_PARAM(Timeout,  WARP_TIMEOUT,       300000);
	// The gap between two looks at the same thing: the route panel while it
	// is empty, and the warp message while the ship is in warp.
	PROG_PARAM(Interval, RECHECK_INTERVAL,     2000);
	PROG_PARAM(Pause,    GATE_JUMP_PAUSE,      5000);
	PROG_PARAM(Pause,    DOCKING_PAUSE,       10000);
	PROG_PARAM(Timeout,  DOCKING_TIMEOUT,     40000);
};
