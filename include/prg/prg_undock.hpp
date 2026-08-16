/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram - leave the station and confirm the ship is in space.

	The undock button is the evidence that the ship is docked and the ship
	core the evidence that it is not, so finding one and then the other is a
	complete undock. Running this program is therefore a way of asking for
	the ship to be in space rather than for a button to be pressed: no
	undock button far more often means the ship was never docked, which the
	one look at the ship core settles without spending the whole budget.

	The click is not a confirmed one. For the seconds the undock animation
	lasts the screen shows neither pattern, so the only proof available is
	the ship core at the end of it, which IN_SPACE_TIMEOUT waits for.
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

	// What can be tuned in prg_params.json, and what it is without it.
	// UNDOCK_CLICK_PAUSE is the only time this program spends on purpose;
	// the two timeouts are only ever paid when something is wrong.
	PROG_PARAM(Timeout, UNDOCK_BUTTON_TIMEOUT,  5000);
	PROG_PARAM(Pause,   UNDOCK_CLICK_PAUSE,     7000);
	PROG_PARAM(Timeout, IN_SPACE_TIMEOUT,      45000);
};
