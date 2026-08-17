/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramDefaults - the numbers every program starts from.

	How long the interface takes to catch up is a property of the client and
	the machine rather than of any one program, so these live in
	eve_config.json and a program only names a timeout of its own when it
	wants something other than the usual.
*/

#pragma once

#include "common_defs.hpp"

// Named like a program's own parameters, and read through common(), so a
// call site shows where a number comes from: MAX_JUMP_TIMEOUT belongs to
// this program, common().CONFIRM_TIMEOUT belongs to every program.
struct ProgramDefaults {
	// Confirming a click: how long to wait for the proof, for each attempt.
	eb::Millis CONFIRM_TIMEOUT {3000};
	// How long a click leaves the interface alone before the proof is
	// looked for, when the call site does not give another value. A press
	// needs more time than a look: the panel it opened must be drawn before
	// a search can say whether it is there.
	eb::Millis WAIT_CLICK      {100};
	// Further clicks to make when that proof does not come.
	int        ACTION_RETRIES  {3};
};
