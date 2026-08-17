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
	const Budget search {UNDOCK_BUTTON_TIMEOUT};
	if (visible(SHIPCORE, eb::Scope::BOX_THEN_FULL))
		return done("already in space, nothing to undock");
	click(UNDOCK, search, UNDOCK_CLICK_PAUSE, {SHIPCORE}, IN_SPACE_TIMEOUT);

	return done("undocked");
}
