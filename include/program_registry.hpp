/*
	EVE bots for Windows.
	Author: Igor Polev.

	The list of programs the application knows about.

	Every program lives in prg/, and this is the only place that says which
	programs exist. Adding one takes two edits: its own pair of files, and
	one line here.
*/

#pragma once

#include "program.hpp"

// Adds every known program to the runner, in the order they are listed.
void add_programs(ProgramRunner& runner);
