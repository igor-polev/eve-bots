/*
	EVE bots for Windows.
	Author: Igor Polev.

	The list of programs the application knows about.

	Every program lives in prg/, and this is the one place that says which
	of them exist. Adding a program is then two edits - its own pair of
	files, and one line here - and nothing outside prg/ has to name it.
*/

#pragma once
#include "program.hpp"

// Registers every known program with the runner, in listing order.
void add_programs(ProgramRunner& runner);
