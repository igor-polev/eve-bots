/*
	EVE bots for Windows.
	Author: Igor Polev.

	The known programs.
*/

#include <memory>

#include "prg/prg_autopilot.hpp"
#include "prg/prg_undock.hpp"
#include "program_registry.hpp"

void add_programs(ProgramRunner& runner)
{
	// This order is the one 'programs' lists and the menu shows, so the
	// simplest program comes first.
	runner.add(std::make_unique<UndockProgram>());
	runner.add(std::make_unique<AutopilotProgram>());
}
