/*
	EVE bots for Windows.
	Author: Igor Polev.

	The known programs.
*/

#include <memory>

#include "prg/prg_autopilot.hpp"
#include "prg/prg_registry.hpp"
#include "prg/prg_undock.hpp"

void add_programs(ProgramRunner& runner)
{
	// Order is the order 'programs' lists them in and the menu shows them,
	// so the simplest first.
	runner.add(std::make_unique<UndockProgram>());
	runner.add(std::make_unique<AutopilotProgram>());
}
