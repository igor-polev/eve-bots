/*
	EVE bots for Windows.
	Author: Igor Polev.

	AutopilotProgram - fly the set route until the ship docks.

	The route panel shows one icon for the next waypoint: a gate when the
	next hop is a jump, a station when it is the destination. Which of the
	two is on screen decides the whole of the rest of the pass - which
	command button to press afterwards, how long to wait once warp ends,
	and whether arriving means the route is finished.

	A hop begins by asking the panel which it is: the gate first, because
	it is the common case by far, and only then the station. A station is
	drawn with one of two icons depending on whether it is the home
	station, and the difference means nothing to flying a route, so those
	two go into one search rather than costing a pass each.

	One pass through the loop is one hop:

	    route icon -> click -> jump or dock button -> click
	    -> warp starts -> warp ends -> settle -> next hop

	Both clicks are confirmed ones, and each is confirmed by whatever it
	was supposed to produce: clicking the route icon has to bring up the
	command button for that kind of hop, and pressing that button has to
	put the warp vector message on screen. Neither needs a search of its
	own afterwards - the confirmation already found the thing and says
	where it is, which is exactly what the next step needs. A click that
	does not show is repeated rather than reported, since a press lost
	between two rendered frames is the ordinary way for one to fail.

	Warp is then watched from both sides, and the two share one
	WARP_TIMEOUT budget. First the warp message has to appear, which says
	the ship really is on its way; then it has to go away, which says it
	arrived. Watching only for its arrival would read a warp that never
	started as success. One budget rather than two because the pair of
	them is a single wait - a warp that starts late leaves less time to
	finish, and what matters is how long the hop has taken in all.

	The loop ends when a station hop finishes and the undock button comes
	back, because that button is the proof the ship is docked. A route
	that ends anywhere else - a gate, an empty route panel - runs out of
	DESTINATION_TIMEOUT and reports failure, since this program has no
	other way to tell "arrived" from "the interface is late".
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
	// What the route panel says is next, waited for until it says
	// something. Which icon it turned out to be is the whole of the
	// difference between the two kinds of hop.
	Sighting next_waypoint();

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

	// What can be tuned in prog_params.json, and what it is without it.
	// The two pauses are the whole of what a hop spends on purpose, so
	// they are where a route gets faster; the timeouts only cost anything
	// when something has gone wrong. How long a click waits for its
	// confirmation and how many times it is repeated are not here at all:
	// those are the same everywhere and come from eve_config.json.
	PROG_PARAM(Timeout,  DESTINATION_TIMEOUT, 10000);
	PROG_PARAM(Timeout,  WARP_TIMEOUT,       300000);
	// The gap between two looks at the same thing: the route panel while
	// it is empty, and the warp message while the ship is in warp.
	PROG_PARAM(Interval, RECHECK_INTERVAL,     2000);
	PROG_PARAM(Pause,    GATE_JUMP_PAUSE,      5000);
	PROG_PARAM(Pause,    DOCKING_PAUSE,       10000);
	PROG_PARAM(Timeout,  DOCKING_TIMEOUT,     40000);
};
