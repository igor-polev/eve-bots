/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram - leave the station and confirm the ship is in space.

	The undock button is the evidence that the ship is docked, and the ship
	core the evidence that it is not: neither can be seen from the other
	side, so finding one and then the other is a complete undock.

	No undock button is not a failure by itself - far more often it means
	the ship was never docked. So the first pass is one look at each: the
	button, and then the ship core, which answers "are we already out?"
	immediately instead of spending the whole budget retrying a button
	that is never going to appear. That makes running this program a way
	of asking for the ship to be in space rather than for a button to be
	pressed.

	Only when neither shows up on that first pass is retrying worthwhile,
	because then the likely cause is the interface still catching up.
	Retries go back to the button alone, until it appears or the search
	budget is gone. Waiting for space after the click retries too, since
	undocking takes seconds of animation during which nothing
	recognisable is on screen.

	Everything before the click shares one UNDOCK_BUTTON_TIMEOUT budget,
	so a client showing neither pattern still gives up on time.

	The click is not a confirmed one. There is nothing to confirm it with:
	for the seconds the undock animation lasts the screen shows neither
	the button nor the ship core, so the only proof available is the ship
	core at the end of it, which is what IN_SPACE_TIMEOUT waits for.
*/

#pragma once
#include "program.hpp"

class UndockProgram : public Program {
public:
	UndockProgram() : Program("undock") {}

	std::string purpose() const override;

protected:
	ProgramResult run() override;

private:
	// What this program works with, named as eve_images.json names it.
	Pattern UNDOCK   {*this, "undock"};
	Pattern SHIPCORE {*this, "shipcore"};

	// What can be tuned in prog_params.json, and what it is without it.
	// UNDOCK_CLICK_PAUSE is the only time this program spends on purpose;
	// the two timeouts are only ever paid when something is wrong.
	PROG_PARAM(Timeout, UNDOCK_BUTTON_TIMEOUT, 15000);
	PROG_PARAM(Pause,   UNDOCK_CLICK_PAUSE,     1000);
	PROG_PARAM(Timeout, IN_SPACE_TIMEOUT,      45000);
};
