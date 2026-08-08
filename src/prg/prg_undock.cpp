/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram implementation.
*/

#include "prg/prg_undock.hpp"

std::string UndockProgram::purpose() const
{
	return "leave the station and confirm the ship reached space";
}

ProgramResult UndockProgram::run()
{
	// Everything up to the click shares one budget, so a client showing
	// nothing recognisable still gives up on time.
	const Budget search {UNDOCK_BUTTON_TIMEOUT};

	// We are supposed to be docked, so look for the undock button once.
	cv::Point corner;
	if (!sighted("the undock button", UNDOCK, search.left(), corner)) {
		// Not there. Far more often that means the ship was never docked
		// than that the button is late, and one look at the ship core
		// settles it without spending the whole budget retrying a button
		// that is never going to appear.
		cv::Point core;
		if (sighted("the ship core", SHIPCORE, search.left(), core))
			return done("already in space, nothing to undock");
		// Neither pattern is on screen. Now late drawing is the likely
		// explanation, so go back to the button and keep trying.
		corner = appear("the undock button", UNDOCK, search);
	}

	// Click the middle of it, and give the game long enough to take the
	// click - not to finish undocking, which is what the wait below is
	// for.
	click("the undock button", UNDOCK, corner);
	const Budget out {IN_SPACE_TIMEOUT};
	pause(UNDOCK_CLICK_PAUSE, "the click to be taken");

	// The ship core is what says the ship made it out. Whatever the click
	// and the pause already used comes off the budget.
	appear("the ship core", SHIPCORE, out);

	return done("undocked");
}
