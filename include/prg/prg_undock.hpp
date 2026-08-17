/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram - leave the station and confirm the ship is in space.

	The undock button proves that the ship is docked, and the ship core
	proves that it is not. So finding one and then the other is a complete
	undock. Running this program asks for the ship to be in space, not for a
	button to be pressed: no undock button usually means the ship was never
	docked, and one look at the ship core settles that without spending the
	whole budget.

	The click is confirmed by the ship core, not by a button. While the
	undock animation runs, for several seconds, neither pattern is on
	screen, so the only proof is the ship core at the end of it, and
	IN_SPACE_TIMEOUT is how long to wait for it.
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
	// The images this program uses, named as eve_images.json names them.
	Pattern UNDOCK   {*this, "undock"};
	Pattern SHIPCORE {*this, "shipcore"};

	// What prg_params.json can change, and the value without it.
	// UNDOCK_CLICK_PAUSE is the only time this program spends on purpose;
	// the two timeouts are paid only when something goes wrong.
	PROG_PARAM(Timeout, UNDOCK_BUTTON_TIMEOUT,  5000);
	PROG_PARAM(Pause,   UNDOCK_CLICK_PAUSE,     7000);
	PROG_PARAM(Timeout, IN_SPACE_TIMEOUT,      45000);
};
