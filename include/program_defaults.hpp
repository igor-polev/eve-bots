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

// Named the way a program's own parameters are, and read through common(),
// so that a call site says which of the two a number came from:
// MAX_JUMP_TIMEOUT is this program's, common().CONFIRM_TIMEOUT is
// everybody's.
struct ProgramDefaults {
	// How long a program gives the interface to have something on screen
	// when it has no reason to want longer.
	eb::Millis ACTION_TIMEOUT  {2000};
	// Confirming a click: how long the proof of it is waited for, per
	// attempt.
	eb::Millis CONFIRM_TIMEOUT {3000};
	// Further clicks to make when that proof does not come.
	int        ACTION_RETRIES  {3};
};
